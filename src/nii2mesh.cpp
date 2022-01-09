// g++ -O1 -g -fsanitize=address -fno-omit-frame-pointer -DHAVE_ZLIB nii2mesh.cpp meshify.cpp base64.cpp bwlabel.cpp -o nii2mesh -lz
// g++ -O3 -DHAVE_ZLIB nii2mesh.cpp meshify.cpp base64.cpp bwlabel.cpp -o nii2mesh -lz

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
	float mn = img32[0];
	float mx = mn;
	for (int i = 0; i < nvox; i++) {
		mn = fmin(mn, img32[i]);
		mx = fmax(mx, img32[i]);
	}
	hdr->cal_min = mn;
	hdr->cal_max = mx;
	return img32;
}

int nii2 (nifti_1_header hdr, float * img, float isolevel, float reduceFraction, int preSmooth, bool onlyLargest, bool fillBubbles, int postSmooth, bool verbose, char * outnm) {
	vec3d *pts = NULL;
	vec3i *tris = NULL;
	int ntri, npt;
	if (meshify(img, &hdr, isolevel, &tris, &pts, &ntri, &npt, preSmooth, onlyLargest, fillBubbles, verbose) != EXIT_SUCCESS)
		return EXIT_FAILURE;
	apply_sform(tris, pts, ntri, npt, hdr.srow_x, hdr.srow_y, hdr.srow_z);
	#ifdef USE_TIMERS
		double startTime = clockMsec();
	#endif
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
			double agressiveness = 7.0; //7 = default for Simplify.h
			int startSize = Simplify::triangles.size();
			int target_count = round((float)Simplify::triangles.size() * reduceFraction);
			Simplify::simplify_mesh(target_count, agressiveness, verbose);
			if (verbose)
				printf("Simplified: %zu vertices, %zu triangles (%f reduction)\n",Simplify::vertices.size(), Simplify::triangles.size()
				, (float)Simplify::triangles.size()/ (float) startSize );
		}
		Simplify::get_raw(&pts, &tris, &npt, &ntri);
	}
	#ifdef USE_TIMERS
		printf("simplify: %ld ms\n", timediff(startTime, clockMsec()));
		startTime = clockMsec();
	#endif
	save_mesh(outnm, tris, pts, ntri, npt);
	#ifdef USE_TIMERS
		printf("save to disk: %ld ms\n", timediff(startTime, clockMsec()));
	#endif
	free(tris);
	free(pts);
	return EXIT_SUCCESS;
}

int main(int argc,char **argv) {
	float isolevel = NAN;
	float reduceFraction = 0.25;
	int preSmooth = true;
	bool onlyLargest = true;
	bool fillBubbles = false;
	int postSmooth = 0;
	bool verbose = false;
	bool isAtlas = false;
	// Check the command line, minimal is name of input and output files
	if (argc < 3) {
		printf("Converts a NIfTI voxelwise image to triangulated mesh.\n");
		printf("Usage: %s [options] niftiname meshname\n",argv[0]);
		printf("Options\n");
		printf("    -a v    atlas of indexed integers (0=not atlas, 1=atlas, default %d)\n", isAtlas);
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
		if (strcmp(argv[i],"-a") == 0)
			isAtlas = atoi(argv[i+1]);
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
	#ifdef USE_TIMERS
		double startTime = clockMsec();
	#endif
	float * img = load_nii(argv[argc-2], &hdr);
	#ifdef USE_TIMERS
		printf("load from disk: %ld ms\n", timediff(startTime, clockMsec()));
	#endif
	if (img == NULL)
		exit(EXIT_FAILURE);
	int ret = EXIT_SUCCESS;
	if (isAtlas) {
		onlyLargest = false;
		if ((hdr.cal_min < 0.0) || (hdr.cal_max < 1.0) || ((hdr.cal_max - hdr.cal_min) < 1.0)) {
			printf("intensity range not consistent with an indexed atlas %g..%g\n", hdr.cal_min, hdr.cal_max);
			exit(EXIT_FAILURE);
		}
		char outnm[768], basenm[768], ext[768] = "";
		strcpy(basenm, argv[argc-1]);
		strip_ext(basenm); // ~/file.nii -> ~/file
		if (strlen(argv[argc-1]) > strlen(basenm))
			strcpy(ext, argv[argc-1] + strlen(basenm));
		int nvox = (hdr.dim[1] * hdr.dim[2] * hdr.dim[3]);
		float * imgbinary = (float *) malloc(nvox*sizeof(float));
		int nOK = 0;
		int nLabel = trunc(hdr.cal_max);
		for (int i = 1; i <= nLabel; i++) {
			int n1 = 0;
			float lo = i - 0.5;
			float hi = i + 0.5;
			for (int j = 0; j < nvox; j++) {
				int n = 0;
				if ((img[j] > lo) && (img[j] < hi))
					n = 1;
				imgbinary[j] = n;
				n1 += n;
			}
			if (n1 == 0) {
				printf("Skipping %d: no voxels with this intensity\n", i);
				continue;
			}
			snprintf(outnm,sizeof(outnm),"%s%d%s", basenm, i, ext);
			int reti = nii2(hdr, imgbinary, 0.5, reduceFraction, preSmooth, onlyLargest, fillBubbles, postSmooth, verbose, outnm);
			if (reti == EXIT_SUCCESS)
				nOK ++;
		}
		if (verbose)
			printf("Converted %d regions of interest\n", nOK);
		if (nOK == 0)
			ret = EXIT_FAILURE;
		free(imgbinary);
	} else
		ret = nii2(hdr, img, isolevel, reduceFraction, preSmooth, onlyLargest, fillBubbles, postSmooth, verbose, argv[argc-1]);

	//argv[argc-1]
	free(img);
	exit(ret);
}
