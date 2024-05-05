#include "image.h"

namespace ELB {

int FFPtr::bayerNameToValue(const std::string &name) {
	static std::map<std::string, int> map = {
		{ "RGGB", cv::COLOR_BayerRG2RGB },
		{ "GRBG", cv::COLOR_BayerGR2RGB },
		{ "BGGR", cv::COLOR_BayerBG2RGB },
		{ "GBRG", cv::COLOR_BayerGB2RGB },
	};
	if ( ! map.count(name) ) {
		throw std::runtime_error(std::string("Unsupported Bayer pattern: ") + name);
	}
	return map[name];
}

std::string FFPtr::fitsError() {
    if ( m_status == 0 ) {
        throw std::runtime_error("0 is not a fits error code");
    }
    char text[STRBUFF];
    fits_get_errstatus(m_status, text);
    return std::string(text);
}

void FFPtr::check() {
    check("Error: ");
}

void FFPtr::check(const std::string &message) {
    if ( m_status == 0 ) {
        return;
    }
    throw FitsError(message + ": " + fitsError() + " (" + m_fname + ")", m_status);
}

FFPtr::FFPtr(const std::string &fname) {
    m_fname = fname;
    fits_open_file(&m_ffptr, fname.c_str(), READONLY, &m_status);
    check("Error opening file");

	long fpix[2] = {1,1};
	long lpix[2];
	long inc[2] = {1,1};
	int bitpix = 0;
	pixfloat nullval = 0;
	int anynull = 0;

	// X and Y is swapped for FITS
	read_key("NAXIS1", TLONG, &m_dimY, NULL);
	read_key("NAXIS2", TLONG, &m_dimX, NULL);
	lpix[0] = m_dimY;
	lpix[1] = m_dimX;
	m_nPix = m_dimX * m_dimY;
	read_key("BITPIX", TINT, &bitpix, NULL);
	auto rawData = std::make_unique<pixfloat[]>(m_nPix);
	read_subset(fitsfloat, fpix, lpix, inc, &nullval, rawData.get(), &anynull);

    m_scale = 1. / (1<<bitpix) * (1<<16);
	m_data = std::make_unique<cv::Mat>(m_dimX, m_dimY, CV_16U);
	for ( long ii=0; ii<m_dimX; ii++) {
		for ( long jj=0; jj<m_dimY; jj++) {
			m_data->at<ushort>(ii, jj) = rawData[ii*m_dimY + jj] * m_scale;
		}
	}
	debayerIfNecessary();
	stretch();
	resample();
}

void FFPtr::debayerIfNecessary() {
	try {
		char buff[32];
		read_key("BAYERPAT", TSTRING, &buff, NULL);
		m_bayerPat = buff;
	} catch ( const FFPtr::FitsError &e ) {
		resetStatus();
		return;
	}
	int pattern = bayerNameToValue(m_bayerPat);
	auto debayered = std::make_unique<cv::Mat>();
	cv::cvtColor(*m_data.get(), *debayered, pattern);
	m_data = std::move(debayered);
}

// PixInsight MTF style autostretch
void FFPtr::stretch() {
	double targetBkg = 0.25;
	double shadows_clip = -1.25;
	std::vector<cv::Mat> channels;
	cv::split(*m_data.get(), channels);
	for ( auto &channel : channels ) {
		channel.convertTo(channel, CV_32F, 1./(1<<16));
		cv::Mat tmp = channel.clone();
		std::vector<float> flat(tmp.begin<float>(), tmp.end<float>());
		std::sort(flat.begin(), flat.end());
		float median = flat[flat.size() / 2]; // Ignore the special median cause of flat.size() % 2 == 0
		float avgdev = medianDeviation(flat, median);
		float c0 = median + shadows_clip * avgdev;
		float m = (targetBkg - 1) * (median - c0) / ((((2 * targetBkg) - 1) * (median - c0)) - targetBkg);
		for ( long ii=0; ii<m_dimX; ii++ ) {
			for ( long jj=0; jj<m_dimY; jj++ ) {
				double val = channel.at<float>(ii,jj);
				if ( val < c0 ) {
					channel.at<float>(ii,jj) = 0;
					continue;
				}
				if ( val == m ) {
					channel.at<float>(ii,jj) = 0.5;
					continue;
				}
				float out = (m - 1) * (val - c0)/(1 - c0) / ((((2 * m) - 1) * (val - c0)/(1 - c0)) - m);
				channel.at<float>(ii,jj) = out;
			}
		}
		channel.convertTo(channel, CV_16U, 1<<16);
	}
	cv::merge(channels, *m_data.get());
}

void FFPtr::resample() {
	long targetWidth = 300;
	long targetHeight = m_dimX * targetWidth / m_dimY;
	m_data->convertTo(*m_data.get(), CV_8U, 1./(1<<8));
	cv::resize(*m_data.get(), *m_data.get(), cv::Size(targetWidth, targetHeight),
			0., 0., cv::InterpolationFlags::INTER_LANCZOS4);
	m_dimX = targetWidth;
	m_dimY = targetHeight;
	m_nPix = m_dimX * m_dimY;
	cv::imwrite("/tmp/debug.tiff", *m_data.get());
}

FFPtr::~FFPtr() {
    fits_close_file(m_ffptr, &m_status);
    check("Error closing file");
}

void FFPtr::read_key(const std::string &key, int datatype, void *value,
        char *comment) {
    fits_read_key(m_ffptr, datatype, key.c_str(), value, comment, &m_status);
    check("Error reading key " + key);
}

void FFPtr::write_key(int datatype, const std::string &key, void *value,
        char *comment) {
    fits_write_key(m_ffptr, datatype, key.c_str(), value, comment, &m_status);
    check("Error writing key " + key);
}

void FFPtr::read_subset(int datatype, long *fpix, long *lpix, long *inc,
        void *nullval, void *data, int *anynull) {
    fits_read_subset(m_ffptr, datatype, fpix, lpix, inc, nullval, data,
            anynull, &m_status);
    check("Error reading pixel data");
}

void FFPtr::get_hdrspace(int *numKeys, int *moreKeys) {
    fits_get_hdrspace(m_ffptr, numKeys, moreKeys, &m_status);
    check("Error getting header size");
}

void FFPtr::read_record(int ii, char *buffer) {
    fits_read_record(m_ffptr, ii, buffer, &m_status);
    check("Error reading header record");
}

void FFPtr::read_keyn(int ii, char *name, char *value, char *comment) {
    fits_read_keyn(m_ffptr, ii, name, value, comment, &m_status);
    check("Error reading key number " + std::to_string(ii));
}

void FFPtr::create_img(int bitpix, int naxis, long *naxes) {
    fits_create_img(m_ffptr, bitpix, naxis, naxes, &m_status);
    check("Error creating image HDU");
}

void FFPtr::write_record(const char *card) {
    fits_write_record(m_ffptr, card, &m_status);
    check("Error writing card");
}

void FFPtr::write_img(int datatype, LONGLONG firstelement,
        LONGLONG nelements, void *data) {
    fits_write_img(m_ffptr, datatype, firstelement, nelements, data, &m_status);
    check();
}

int FFPtr::status() {
    return m_status;
}

void FFPtr::resetStatus() {
    m_status = 0;
}

pixfloat FFPtr::mean() {
	pixfloat mean = 0.0;
	for ( long ii=0; ii<m_dimX; ii++ ) {
		for ( long jj=0; jj<m_dimY; jj++ ) {
			mean += m_data->at<pixfloat>(ii,jj);
		}
	}
	return mean / m_nPix;
}

float FFPtr::medianDeviation(const std::vector<float> &data, float median) {
	double ret = 0;
	for ( const auto &val : data ) {
		ret += fabs(val - median);
	}
	return ret / data.size();
}

FFPtr::FitsError::FitsError(const std::string &message, int status) : std::runtime_error(message) {
	m_status = status;
	m_message = message;
}

const char *FFPtr::FitsError::what() const noexcept {
	return m_message.c_str();
}

int FFPtr::FitsError::status() const noexcept {
	return m_status;
}

}
