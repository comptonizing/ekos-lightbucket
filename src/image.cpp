#include "image.h"

namespace ELB {

std::string Image::fitsError(int status) {
	char buff[256];
	if ( status == 0 ) {
		throw std::runtime_error("0 is not a fits error code");
	}
	fits_get_errstatus(status, buff);
	return std::string(buff);
}

Image::Image(const std::string &fileName) : m_fileName(fileName) {
	long fpix[2] = {1,1};
	long lpix[2];
	long inc[2] = {1,1};
	int bitpix = 0;
	pixfloat nullval = 0;
	int anynull = 0;

	FFPtr ffptr(fileName);
	ffptr.read_key("NAXIS1", TLONG, &m_dimX, NULL);
	ffptr.read_key("NAXIS2", TLONG, &m_dimY, NULL);
	lpix[0] = m_dimX;
	lpix[1] = m_dimY;
	m_nPix = m_dimX * m_dimY;
	ffptr.read_key("BITPIX", TINT, &bitpix, NULL);
	m_data = std::make_unique<pixfloat[]>(m_nPix);
	ffptr.read_subset(fitsfloat, fpix, lpix, inc, &nullval, m_data.get(), &anynull);

    pixfloat * __restrict__ data = m_data.get();
    // bitpix larger 0 means integer, so rescale values
    if ( bitpix > 0 ) {
        m_scale = 1. / (1<<bitpix);
        for (long ii=0; ii<m_nPix; ii++) {
            data[ii] = m_scale * m_data[ii];
        }
    }
}

std::string Image::FFPtr::fitsError() {
    if ( m_status == 0 ) {
        throw std::runtime_error("0 is not a fits error code");
    }
    char text[STRBUFF];
    fits_get_errstatus(m_status, text);
    return std::string(text);
}

void Image::FFPtr::check() {
    check("Error: ");
}

void Image::FFPtr::check(const std::string &message) {
    if ( m_status == 0 ) {
        return;
    }
    throw std::runtime_error(message + ": " + fitsError() + " (" + m_fname + ")");
}

Image::FFPtr::FFPtr(const std::string &fname) {
    m_fname = fname;
    fits_open_file(&m_ffptr, fname.c_str(), READONLY, &m_status);
    check("Error opening file");
}

Image::FFPtr::FFPtr(fitsfile *ffptr, int status, const std::string &fname) {
    m_fname = fname;
    m_ffptr = ffptr;
    m_status = status;
    check("Error creating file");
}

Image::FFPtr::~FFPtr() {
    fits_close_file(m_ffptr, &m_status);
    check("Error closing file");
}

void Image::FFPtr::read_key(const std::string &key, int datatype, void *value,
        char *comment) {
    fits_read_key(m_ffptr, datatype, key.c_str(), value, comment, &m_status);
    check("Error reading key " + key);
}

void Image::FFPtr::write_key(int datatype, const std::string &key, void *value,
        char *comment) {
    fits_write_key(m_ffptr, datatype, key.c_str(), value, comment, &m_status);
    check("Error writing key " + key);
}

void Image::FFPtr::read_subset(int datatype, long *fpix, long *lpix, long *inc,
        void *nullval, void *data, int *anynull) {
    fits_read_subset(m_ffptr, datatype, fpix, lpix, inc, nullval, data,
            anynull, &m_status);
    check("Error reading pixel data");
}

void Image::FFPtr::get_hdrspace(int *numKeys, int *moreKeys) {
    fits_get_hdrspace(m_ffptr, numKeys, moreKeys, &m_status);
    check("Error getting header size");
}

void Image::FFPtr::read_record(int ii, char *buffer) {
    fits_read_record(m_ffptr, ii, buffer, &m_status);
    check("Error reading header record");
}

void Image::FFPtr::read_keyn(int ii, char *name, char *value, char *comment) {
    fits_read_keyn(m_ffptr, ii, name, value, comment, &m_status);
    check("Error reading key number " + std::to_string(ii));
}

void Image::FFPtr::create_img(int bitpix, int naxis, long *naxes) {
    fits_create_img(m_ffptr, bitpix, naxis, naxes, &m_status);
    check("Error creating image HDU");
}

void Image::FFPtr::write_record(const char *card) {
    fits_write_record(m_ffptr, card, &m_status);
    check("Error writing card");
}

void Image::FFPtr::write_img(int datatype, LONGLONG firstelement,
        LONGLONG nelements, void *data) {
    fits_write_img(m_ffptr, datatype, firstelement, nelements, data, &m_status);
    check();
}

int Image::FFPtr::status() {
    return m_status;
}

void Image::FFPtr::resetStatus() {
    m_status = 0;
}

}
