// g++ -O1 -g -fsanitize=address -fno-omit-frame-pointer example.cpp meshify.cpp base64.cpp -o exam; ./exam -i 64 bet.nii mymesh.stl
// g++ -O3 nii2mesh.cpp meshify.cpp base64.cpp -o nii2mesh; ./nii2mesh -v -r 0.9 bet.nii mymesh.mz3

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include "meshify.h"
#include "nifti1.h"
#include "Simplify.h"
#ifdef HAVE_ZLIB
	#include <zlib.h>
#endif

float * load_nii(const char *fnm, nifti_1_header * hdr) {
	char imgnm[768], hdrnm[768], basenm[768], ext[768] = "";
	strcpy(basenm, fnm);
	strcpy(imgnm, fnm);
	strcpy(hdrnm, fnm);
	strip_ext(basenm); // ~/file.nii -> ~/file
	if (strlen(fnm) > strlen(basenm))
		strcpy(ext, fnm + strlen(basenm));
	if (strstr(ext, ".hdr")) {
		strcpy(imgnm, basenm);
		strcat(imgnm, ".img");
	}
	if (strstr(ext, ".img")) {
		strcpy(hdrnm, basenm);
		strcat(hdrnm, ".hdr");
	}
	if( access( hdrnm, F_OK ) != 0 ) {
		printf("Unable to find a file named %s\n", hdrnm);
		return NULL;
	}
	if( access( imgnm, F_OK ) != 0 ) {
		printf("Unable to find a file named %s\n", imgnm);
		return NULL;
	}
	bool isGz = false;
	#ifdef HAVE_ZLIB
	if (strstr(ext, ".gz")) {
		gzFile fgz = gzopen(hdrnm, "r");
		if (! fgz) {
			printf("gzopen error %s\n", hdrnm);
			return NULL;
		}
		int bytes_read = gzread(fgz, hdr, sizeof(nifti_1_header));
		gzclose(fgz);
		isGz = true;
	}
	#endif
	if (!isGz) {
		FILE *fp = fopen(hdrnm,"rb");
		if (fp == NULL)
			return NULL;
		fread(hdr, sizeof(nifti_1_header), 1, fp);
		fclose(fp);
	}
	uint16_t sig = 348;
	uint16_t fwd = hdr->sizeof_hdr;
	uint16_t rev = (fwd & 0xff) << 8 | ((fwd & 0xff00) >> 8);
	if (rev == sig) {
		printf("Demo only reads native endian NIfTI (solution: use niimath)\n");
		return NULL;
	}
	if (fwd != sig) {
		printf("Demo only reads uncompressed NIfTI (solution: 'gzip -d \"%s\"')\n", hdrnm);
		return NULL;
	}
	if ((hdr->datatype != DT_UINT8) && (hdr->datatype != DT_UINT16) && (hdr->datatype != DT_INT16) && (hdr->datatype != DT_FLOAT32)) {
		printf("Demo does not support this data type (solution: use niimath)\n");
		return NULL;
	}
	int nvox = hdr->dim[1]*hdr->dim[2]*hdr->dim[3];
	if (hdr->scl_slope == 0.0) hdr->scl_slope = 1.0;
	float * img32 = (float *) malloc(nvox*sizeof(float));
	int bpp = 2;
	if (hdr->datatype == DT_UINT8)
		bpp = 1;
	if (hdr->datatype == DT_FLOAT32)
		bpp = 4;
	void * imgRaw = (void *) malloc(nvox*bpp);
	if (isGz) {
		gzFile fgz = gzopen(hdrnm, "r");
		if (! fgz)
			return NULL;
		if (hdr->vox_offset > 0) { //skip header
			int nskip = round(hdr->vox_offset);
			void * skip = (void *) malloc(nskip);
			int bytes_read = gzread (fgz, skip, nskip);
			if (bytes_read != nskip)
				return NULL;
		}
		int bytes_read = gzread(fgz, imgRaw, nvox*bpp);
		if (bytes_read != (nvox*bpp))
			return NULL;
		gzclose(fgz);
	} else {
		FILE *fp = fopen(imgnm,"rb");
		if (fp == NULL)
			return NULL;
		fseek(fp, (int)hdr->vox_offset, SEEK_SET);
		fread(imgRaw, nvox*bpp, 1, fp);
		fclose(fp);
	}
	if (hdr->datatype == DT_UINT8) {
		uint8_t * img8 = (uint8_t *) imgRaw;
		for (int i = 0; i < nvox; i++)
			img32[i] = (img8[i] * hdr->scl_slope) + hdr->scl_inter;
	} else if (hdr->datatype == DT_UINT16) {
		uint16_t * img16 = (uint16_t *) imgRaw;
		for (int i = 0; i < nvox; i++)
			img32[i] = (img16[i] * hdr->scl_slope) + hdr->scl_inter;
	} else if (hdr->datatype == DT_INT16) {
		int16_t * img16 = (int16_t *) imgRaw;
		for (int i = 0; i < nvox; i++)
			img32[i] = (img16[i] * hdr->scl_slope) + hdr->scl_inter;
	} else {
		float * img32w = (float *) imgRaw;
		for (int i = 0; i < nvox; i++)
			img32[i] = (img32w[i] * hdr->scl_slope) + hdr->scl_inter;
	}
	free(imgRaw);
	return img32;
}

int main(int argc,char **argv) {
	float isolevel = NAN;
	float reduceFraction = 0.25;
	int preSmooth = true;
	bool onlyLargest = true;
	bool fillBubbles = false;
	bool verbose = false;
	int postSmooth = 0;
	// Check the command line, minimal is name of input and output files
	if (argc < 3) {
		printf("Converts a NIfTI voxelwise image to triangulated mesh.\n");
		printf("Usage: %s [options] niftiname meshname\n",argv[0]);
		printf("Options\n");
		printf("    -b v    bubble fill (0=bubbles included, 1=bubbles filled, default %d)\n", fillBubbles);
		printf("    -i v    isosurface intensity (default mid-range)\n");
		printf("    -l v    only keep largest cluster (0=all, 1=largest, default %d)\n", onlyLargest);
		printf("    -p v    pre-smoothing (0=skip, 1=smooth, default %d)\n", preSmooth);
		printf("    -r v    reduction factor (default %g)\n", reduceFraction);
		printf("    -s v    post-smoothing iterations (default %d)\n", postSmooth);
		printf("    -v v    verbose (0=silent, 1=verbose, default %d)\n", verbose);
		printf("mesh extension sets format (.gii, .mz3, .obj, .ply, .pial, .stl, .vtk)\n");
		printf("Example: '%s myInput.nii myOutput.obj'\n",argv[0]);
		printf("Example: '%s -i 22 myInput.nii myOutput.obj'\n",argv[0]);
		printf("Example: '%s -p 0 img.nii out.ply'\n",argv[0]);
		printf("Example: '%s -v 1 img.nii out.ply'\n",argv[0]);
		printf("Example: '%s -r 0.1 img.nii small.gii'\n",argv[0]);
		exit(-1);
	}
	// Parse the command line
	for (int i=1;i<argc;i++) {
		if (strcmp(argv[i],"-b") == 0)
			fillBubbles = atoi(argv[i+1]);
		if (strcmp(argv[i],"-i") == 0)
			isolevel = atof(argv[i+1]);
		if (strcmp(argv[i],"-l") == 0)
			onlyLargest = atoi(argv[i+1]);
		if (strcmp(argv[i],"-p") == 0)
			preSmooth = atoi(argv[i+1]);
		if (strcmp(argv[i],"-s") == 0)
			postSmooth = atoi(argv[i+1]);
		if (strcmp(argv[i],"-r") == 0)
			reduceFraction = atof(argv[i+1]);
		if (strcmp(argv[i],"-v") == 0)
			verbose = atoi(argv[i+1]);
	}
	nifti_1_header hdr;
	float * img = load_nii(argv[argc-2], &hdr);
	if (img == NULL)
		exit(EXIT_FAILURE);
	vec3d *pts = NULL;
	vec3i *tris = NULL;
	int ntri, npt;
	int OK = meshify(img, &hdr, isolevel, &tris, &pts, &ntri, &npt, preSmooth, onlyLargest, fillBubbles, verbose);
	apply_sform(tris, pts, ntri, npt, hdr.srow_x, hdr.srow_y, hdr.srow_z);
	if ((postSmooth > 0) || (reduceFraction < 1.0)) {
		Simplify::load_raw(pts, tris, npt, ntri);
		free(tris);
		free(pts);
		if (postSmooth > 0) {
			if (verbose)
				printf("%d Laplacian HC smoothing iterations.\n", postSmooth);
			Simplify::laplacianHC_smooth(0.5, 0.1, postSmooth);
		}
		if (reduceFraction < 1.0) {
			double agressiveness = 5.0;
			int startSize = Simplify::triangles.size();
			int target_count = round((float)Simplify::triangles.size() * reduceFraction);
			Simplify::simplify_mesh(target_count, agressiveness, verbose);
			if (verbose)
				printf("Simplified: %zu vertices, %zu triangles (%f reduction)\n",Simplify::vertices.size(), Simplify::triangles.size()
				, (float)Simplify::triangles.size()/ (float) startSize );
		}
		Simplify::get_raw(&pts, &tris, &npt, &ntri);
	}
	if (OK == EXIT_SUCCESS)
		OK = save_mesh(argv[argc-1], tris, pts, ntri, npt);
	free(tris);
	free(pts);
	free(img);
	exit(EXIT_SUCCESS);
}
