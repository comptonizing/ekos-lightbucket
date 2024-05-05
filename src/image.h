#pragma once

#include <iostream>
#include <stdexcept>
#include <memory>
#include <map>

#include <fitsio.h>

#include <opencv2/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc.hpp>

#define STRBUFF (256)

#define pixfloat float
#define fitsfloat TFLOAT
#define cvfloat CV_16U

namespace ELB {

    class FFPtr {
        public:
			class FitsError : public std::runtime_error {
				public:
					FitsError(const std::string &message, int status);
					virtual const char* what() const noexcept override;
					int status() const noexcept;
				private:
					int m_status = 0;
					std::string m_message;
			};
			static int bayerNameToValue(const std::string &name);

            FFPtr(const std::string &fname);
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
			pixfloat mean();
        private:
			void debayerIfNecessary();
			void stretch();
			void resample();
            std::string fitsError();
            std::string m_fname;
            fitsfile *m_ffptr = NULL;
            int m_status = 0;
			long m_dimX, m_dimY, m_nPix;
			double m_scale;
			std::unique_ptr<cv::Mat> m_data;
			std::string m_bayerPat = "";

			static float medianDeviation(const std::vector<float> &data, float median);
    };
}
