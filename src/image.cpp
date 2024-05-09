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

	m_initalMean = 0;
    m_valueScale = 1. / (1<<bitpix) * (1<<16);
	m_data = std::make_unique<cv::Mat>(m_dimX, m_dimY, CV_16U);
	for ( long ii=0; ii<m_dimX; ii++) {
		for ( long jj=0; jj<m_dimY; jj++) {
			m_initalMean += rawData[ii*m_dimY + jj] / m_nPix;
			m_data->at<ushort>(ii, jj) = rawData[ii*m_dimY + jj] * m_valueScale;
		}
	}
	debayerIfNecessary();
	stretch();
	blur();
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

void FFPtr::blur() {
	m_data->convertTo(*m_data.get(), CV_8U, 1./(1<<8));
	cv::medianBlur(*m_data.get(), *m_data.get(), 21);
}

void FFPtr::resample() {
	long targetWidth = 300;
	long targetHeight = m_dimX * targetWidth / m_dimY;
	cv::resize(*m_data.get(), *m_data.get(), cv::Size(targetWidth, targetHeight),
			0., 0., cv::InterpolationFlags::INTER_LANCZOS4);
	m_dimX = targetWidth;
	m_dimY = targetHeight;
	m_nPix = m_dimX * m_dimY;
}

std::string FFPtr::encode() {
	std::vector<uchar> jpgBuffer;
	// This returns a bool indicating whether the buffer can be decoded by
	// opencv but we don't really care about that, so ignore it
	cv::imencode(".jpg", *m_data.get(), jpgBuffer, {cv::ImwriteFlags::IMWRITE_JPEG_QUALITY, 70});
	std::string data {jpgBuffer.begin(), jpgBuffer.end()};
	return macaron::Base64::Encode(data);
}

int FFPtr::gain() {
	if ( m_gain != nullptr ) {
		return *m_gain;
	}
	int ret = -1;
	try {
		read_key("GAIN", TINT, &ret, NULL);
	} catch ( const FitsError &e1 ) {
		if ( e1.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();
		try {
			read_key("ISOSPEED", TINT, &ret, NULL);
		} catch ( const FitsError &e2 ) {
			if ( e2.status() != KEY_NO_EXIST ) {
				throw;
			}
			resetStatus();
		}
	}
	m_gain = std::make_unique<int>(ret);
	*m_gain = ret;
	return ret;
}

std::string FFPtr::object() {
	if ( m_object != nullptr ) {
		return *m_object;
	}
	std::string ret;
	try {
		char buff[STRBUFF];
		read_key("OBJECT", TSTRING, buff, NULL);
		ret = std::string(buff);
	} catch ( const FitsError &e ) {
		if ( e.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();
		ret = std::string("");
	}
	m_object = std::make_unique<std::string>(ret);
	return ret;
}

double FFPtr::rotation() {
	if ( m_rotation != nullptr ) {
		return *m_rotation;
	}
	double ret = NAN;
	try {
		read_key("CROTA1", TDOUBLE, &ret, NULL);
	} catch ( FitsError &e ) {
		if ( e.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();
	}
	m_rotation = std::make_unique<double>(NAN);
	return ret;
}

std::string FFPtr::instrument() {
	if ( m_instrument != nullptr ) {
		return *m_instrument;
	}
	std::string ret;
	try {
		char buff[STRBUFF];
		read_key("INSTRUME", TSTRING, buff, NULL);
		ret = std::string(buff);
	} catch ( const FitsError &e ) {
		if ( e.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();
		ret = std::string("");
	}
	m_instrument = std::make_unique<std::string>(ret);
	return ret;
}

std::string FFPtr::telescope() {
	if ( m_telescope != nullptr ) {
		return *m_telescope;
	}
	std::string ret;
	try {
		char buff[STRBUFF];
		read_key("TELESCOP", TSTRING, buff, NULL);
		ret = std::string(buff);
	} catch ( const FitsError &e ) {
		if ( e.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();
		ret = std::string("");
	}
	m_telescope = std::make_unique<std::string>(ret);
	return ret;
}

double FFPtr::focalLength() {
	if ( m_focalLength != nullptr ) {
		return *m_focalLength;
	}
	double ret = NAN;
	try {
		read_key("FOCALLEN", TDOUBLE, &ret, NULL);
	} catch ( const FitsError &e ) {
		if ( e.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();
	}
	m_focalLength = std::make_unique<double>(ret);
	return ret;
}

double FFPtr::aperture() {
	if ( m_aperture != nullptr ) {
		return *m_aperture;
	}
	double ret = NAN;
	try {
		read_key("APTDIA", TDOUBLE, &ret, NULL);
	} catch ( const FitsError &e ) {
		if ( e.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();
	}
	m_aperture = std::make_unique<double>(ret);
	return ret;
}

double FFPtr::pixelSize() {
	if ( m_pixelSize != nullptr ) {
		return *m_pixelSize;
	}
	double ret = NAN;
	try {
		read_key("PIXSIZE1", TDOUBLE, &ret, NULL);
	} catch ( const FitsError &e ) {
		if ( e.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();
	}
	m_pixelSize = std::make_unique<double>(ret);
	return ret;
}

double FFPtr::scale() {
	if ( m_scale != nullptr ) {
		return *m_scale;
	}
	double ret = NAN;
	try {
		read_key("SCALE", TDOUBLE, &ret, NULL);
	} catch ( const FitsError &e ) {
		if ( e.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();
	}
	m_scale = std::make_unique<double>(ret);
	return ret;
}

std::string FFPtr::filter() {
	if ( m_filter != nullptr ) {
		return *m_filter;
	}
	std::string ret;
	try {
		char buff[STRBUFF];
		read_key("FILTER", TSTRING, buff, NULL);
		ret = std::string(buff);
	} catch ( const FitsError &e ) {
		if ( e.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();
		ret = std::string("");
	}
	m_filter = std::make_unique<std::string>(ret);
	return ret;
}

double FFPtr::exposure() {
	if ( m_exposure != nullptr ) {
		return *m_exposure;
	}
	double ret = NAN;
	try {
		read_key("EXPTIME", TDOUBLE, &ret, NULL);
	} catch ( const FitsError &e ) {
		if ( e.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();
	}
	m_exposure = std::make_unique<double>(ret);
	return ret;
}

int FFPtr::offset() {
	if ( m_offset != nullptr ) {
		return *m_offset;
	}
	int ret = -1;
	try {
		read_key("OFFSET", TINT, &ret, NULL);
	} catch ( const FitsError &e ) {
		if ( e.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();
	}
	m_offset = std::make_unique<int>(ret);
	return ret;
}

std::string FFPtr::binning() {
	if ( m_binning != nullptr ) {
		return *m_binning;
	}
	std::string ret;
	try {
		char buff[STRBUFF];
		int x, y;
		read_key("XBINNING", TINT, &x, NULL);
		read_key("YBINNING", TINT, &y, NULL);
		snprintf(buff, sizeof(buff), "%dx%d", x, y);
		ret = std::string(buff);
	} catch ( const FitsError &e ) {
		if ( e.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();
		ret = std::string("");
	}
	m_binning = std::make_unique<std::string>(ret);
	return ret;
}

std::string FFPtr::time() {
	if ( m_time != nullptr ) {
		return *m_time;
	}
	std::string ret;
	try {
		char buff[STRBUFF];
		read_key("DATE-OBS", TSTRING, buff, NULL);
		ret = std::string(buff);
	} catch ( const FitsError &e ) {
		if ( e.status() != KEY_NO_EXIST ) {
			throw;
		}
		resetStatus();

		time_t rawTime;
		struct tm *timeInfo;
		timeInfo = localtime(&rawTime);
		char buff[STRBUFF];
		strftime(buff, sizeof(buff), "%H:%M:%S: ", timeInfo);
		ret = std::string(buff);
	}
	m_time = std::make_unique<std::string>(ret);
	return ret;
}

double FFPtr::initialMean() {
	return m_initalMean;
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
