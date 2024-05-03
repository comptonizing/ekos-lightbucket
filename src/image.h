#pragma once

#include <iostream>
#include <stdexcept>
#include <memory>
#include <fitsio.h>

#define STRBUFF (256)

#define pixfloat float
#define fitsfloat TFLOAT

namespace ELB {

class Image {
	public:
		Image(const std::string &fileName);
	private:
		std::string fitsError(int status);

		std::string m_fileName;
		long m_dimX, m_dimY, m_nPix;
		std::unique_ptr<pixfloat[]> m_data;
		pixfloat m_scale = 0;

    class FFPtr {
        public:
            FFPtr(const std::string &fname);
            FFPtr(fitsfile *ffptr, int status, const std::string &fname);
            ~FFPtr();
            void read_key(const std::string &key, int datatype, void *value,
                    char *comment);
            void write_key(int datatype, const std::string &key, void *value,
                    char *comment);
            void check();
            void check(const std::string &message);
            int status();
            void resetStatus();
            void read_subset(int datatype, long *fpix, long *lpix, long *inc,
                    void *nullval, void *data, int *anynull);
            void get_hdrspace(int *numKeys, int *moreKeys);
            void read_record(int ii, char *buffer);
            void read_keyn(int ii, char *name, char *value, char *comment);
            void create_img(int bitpix, int naxis, long *naxes);
            void write_record(const char *card);
            void write_img(int datatype, LONGLONG firstelement,
                    LONGLONG nelements, void *data);
        private:
            std::string fitsError();
            std::string m_fname;
            fitsfile *m_ffptr = NULL;
            int m_status = 0;
    };
};


}
