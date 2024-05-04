#include "frame.h"
#include "gtkmm/button.h"
#include "gtkmm/dialog.h"
#include "gtkmm/enums.h"
#include "gtkmm/messagedialog.h"
#include "gtkmm/widget.h"

namespace ELB {

FrmMain::FrmMain(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& refGlade) :
		Gtk::ApplicationWindow(cobject), builder(refGlade) {
	builder->get_widget("textLog", m_tvLog);
	builder->get_widget("labelQueueSize", m_labelQueueSize);
	builder->get_widget("labelSuccessSize", m_labelSuccessSize);
	builder->get_widget("labelFailureSize", m_labelFailureSize);
	builder->get_widget("labelProcessing", m_labelProcessing);
	builder->get_widget("spinnerProcessing", m_spinnerProcessing);
	builder->get_widget("buttonLBHelp", m_buttonHelp);
	builder->get_widget("buttonLBSave", m_buttonSave);
	builder->get_widget("entryLBUser", m_entryUser);
	builder->get_widget("entryLBKey", m_entryKey);
	m_dbus = Gio::DBus::Connection::get_sync(Gio::DBus::BUS_TYPE_SESSION);
	if ( ! m_dbus ) {
		showError("DBUS Error", "Error connecting to DBUS!");
		exit(EXIT_FAILURE);
	}
	m_logBuffer = Gtk::TextBuffer::create();
	m_tvLog->set_buffer(m_logBuffer);
	m_dbus->signal_subscribe(
			sigc::mem_fun(*this, &FrmMain::onCaptureComplete),
			m_kstarsName,
			m_captureInterface,
			m_signalName,
			m_capturePath,
			""
			);
	signal_delete_event().connect(sigc::mem_fun(*this, &FrmMain::quit));
	m_buttonHelp->signal_clicked().connect(sigc::mem_fun(*this, &FrmMain::help));
	m_buttonSave->signal_clicked().connect(sigc::mem_fun(*this, &FrmMain::saveConfig));
	Glib::signal_timeout().connect([this]() mutable {
			if ( m_processing.get() ) {
				m_labelProcessing->set_text("Processing");
				m_spinnerProcessing->start();
			} else {
				m_labelProcessing->set_text("Idle");
				m_spinnerProcessing->stop();
			}
			char buff[32];
			snprintf(buff, sizeof(buff), "%lu", m_nSuccess.get());
			m_labelSuccessSize->set_text(buff);
			snprintf(buff, sizeof(buff), "%lu", m_nFailure.get());
			m_labelFailureSize->set_text(buff);
			std::lock_guard<std::mutex> lock(m_queueMutex);
			snprintf(buff, sizeof(buff), "%lu", m_fileQueue.size());
			m_labelQueueSize->set_text(buff);
			return true;
			}, 100);
	m_labelProcessing->set_text("Idle");
	m_spinnerProcessing->stop();
	initConfig();
	m_workerThread = std::thread(&FrmMain::runWorker, this);
}

void FrmMain::initConfig() {
	std::string user, key, more;
	const gchar *args[3];
	args[0] = g_get_user_config_dir();
	args[1] = "ekoslightbucket.conf";
	args[2] = NULL;
	m_configFile = std::string(g_build_filenamev((gchar **) args));
	std::ifstream stream(m_configFile);
	if ( ! stream.is_open() ) {
		log("No exiting configuration available\n");
		return;
	}
	std::getline(stream, user);
	std::getline(stream, key);
	std::getline(stream, more);
	if ( user == "" || key == "" || ! stream.eof()) {
		log("Corrupted configuration detected\n");
		char buff[512];
		snprintf(buff, sizeof(buff),
				"Your configuration file (%s) is corrupted and will be deleted. "
				"You will have to re-enter your username and API key.",
				m_configFile.c_str());
		showError("Corrupted Configuration", buff);
		stream.close();
		std::remove(m_configFile.c_str());
		return;
	}
	m_entryUser->set_text(user);
	m_entryKey->set_text(key);
	log("Found existing configuration\n");
}

void FrmMain::saveConfig() {
	if ( m_entryUser->get_text() == "" || m_entryKey->get_text() == "" ) {
		showError("Configuration Error",
				"You need to enter the username and API key in order to save it");
		return;
	}
	log("Saving configuration\n");
	std::ofstream stream(m_configFile);
	stream << m_entryUser->get_text() << std::endl;
	stream << m_entryKey->get_text() << std::endl;
	stream.close();
}

FrmMain::~FrmMain() {
}

void FrmMain::help() {
	Glib::ustring msg =
		"For the upload to app.lightbucket.co to work correctly you need to "
		"enter your credentials here. These consist of your lightbucket "
		"username and an API key. You can get this information by visiting "
		"https://app.lightbucket.co/api_credentials. By clicking the \"Save\" "
		"button the uploader will remember your credentials and load them "
		"automatically when it starts up.\n\n"
		"Do you want to visit this website now?"
		;
	m_dialog.reset(new Gtk::MessageDialog(*this, msg, false,
				Gtk::MessageType::MESSAGE_INFO, Gtk::ButtonsType::BUTTONS_YES_NO,
				true));
	m_dialog->set_title("Lightbucket Credentials");
	m_dialog->set_modal(true);
	m_dialog->signal_response().connect([this](int response) {
			if ( response == Gtk::ResponseType::RESPONSE_YES ) {
				show_uri("https://app.lightbucket.co/api_credentials", GDK_CURRENT_TIME);
			}
			m_dialog->hide();
			});
	m_dialog->show();
}

void FrmMain::stopWorker() {
	m_shutdown.set(true);
	m_workerThread.join();
}

bool FrmMain::quit(_GdkEventAny* event) {
	int nQueue = 0;
	std::ignore = event;
	m_queueMutex.lock();
	nQueue = m_fileQueue.size();
	m_queueMutex.unlock();
	if ( nQueue > 0 ) {
		showQueueWarning(nQueue);
	} else {
		stopWorker();
		return false;
	}
	return true;
}

void FrmMain::onCaptureComplete(
		const Glib::RefPtr<Gio::DBus::Connection>& connection,
		const Glib::ustring &sender_name,
		const Glib::ustring &object_path,
		const Glib::ustring &interface_name,
		const Glib::ustring &signal_name,
		const Glib::VariantContainerBase& parameters
		) {
	std::ignore = connection;
	std::ignore = sender_name;
	std::ignore = object_path;
	std::ignore = interface_name;
	std::ignore = signal_name;

	Glib::VariantBase inside;
	parameters.get_child(inside, 0);

	auto content = Glib::VariantBase::cast_dynamic<Glib::VariantContainerBase>(inside);

	Glib::ustring fileName = "";
	int type = -1;
	int median = -1;
	int starCount = -1;
	double hfr = -1;

	for ( gsize ii=0; ii<content.get_n_children(); ii++ ) {
		auto thing = Glib::VariantBase::cast_dynamic<Glib::VariantContainerBase>(content.get_child(ii));
		Glib::VariantBase child;
		thing.get_child(child);
		auto name = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(child).get();
		auto value = Glib::VariantBase::cast_dynamic<Glib::VariantContainerBase>(thing.get_child(1)).get_child(0);
		if ( name == "filename" ) {
			fileName = Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(value).get();
			if ( fileName == "/tmp/image.fits" ) {
				// Preview
				return;
			}
		}
		if ( name == "type" ) {
			type = Glib::VariantBase::cast_dynamic<Glib::Variant<int>>(value).get();
			if ( type != 0 ) {
				// Not a light frame
				return;
			}
			continue;
		}
		if ( name == "median" ) {
			median = Glib::VariantBase::cast_dynamic<Glib::Variant<int>>(value).get();
			continue;
		}
		if ( name == "starCount" ) {
			starCount = Glib::VariantBase::cast_dynamic<Glib::Variant<int>>(value).get();
			continue;
		}
		if ( name == "hfr" ) {
			hfr = Glib::VariantBase::cast_dynamic<Glib::Variant<double>>(value).get();
			continue;
		}
	}
	FrameData frameData(fileName, median, starCount, hfr);
	char buff[256];
	snprintf(buff, sizeof(buff), "Queueing file %s\n", fileName.c_str());
	std::lock_guard<std::mutex> lock(m_queueMutex);
	log(buff);
	m_fileQueue.push(frameData);
}

void FrmMain::showError(Glib::ustring title, Glib::ustring message, Glib::ustring secondaryMessage) {
	m_dialog.reset(new Gtk::MessageDialog(*this, message, false,
				Gtk::MessageType::MESSAGE_ERROR, Gtk::ButtonsType::BUTTONS_OK, true));
	m_dialog->set_title(title);
	if ( secondaryMessage != Glib::ustring("") ) {
		m_dialog->set_secondary_text(secondaryMessage);
	}
	m_dialog->set_modal(true);
	m_dialog->signal_response().connect(sigc::hide(sigc::mem_fun(*m_dialog, &Gtk::Widget::hide)));
	m_dialog->show();
}

void FrmMain::showQueueWarning(int nItems) {
	char buff[256];
	snprintf(buff, sizeof(buff), "There are %d files left in the queue", nItems);
	m_dialog.reset(new Gtk::MessageDialog(*this, buff, false,
				Gtk::MessageType::MESSAGE_WARNING, Gtk::ButtonsType::BUTTONS_YES_NO, true));
	m_dialog->set_title("Files left in queue");
	m_dialog->set_secondary_text("Do you want to quit anyway?");
	m_dialog->set_modal(true);
	m_dialog->signal_response().connect([this](int response) mutable {
			if ( response == Gtk::ResponseType::RESPONSE_YES ) {
				m_dialog->hide();
				m_finishDialog.reset(new Gtk::MessageDialog(*this, "Waiting for current file to finish",
						false, Gtk::MessageType::MESSAGE_INFO, Gtk::ButtonsType::BUTTONS_NONE, true));
				m_finishDialog->set_title("Waiting to finish");
				m_finishDialog->set_modal(true);
				m_shutdown.set(true);
				Glib::signal_timeout().connect([this]() mutable {
						if ( m_running.get() ) {
							return true;
						}
						stopWorker();
						Gtk::Main::quit();
						return false;
					}, 100);
				m_finishDialog->show();
			} else {
				this->m_dialog->hide();
			}
			});
	m_dialog->show();
}

void FrmMain::log(const std::string &msg, bool showTimestamp) {
	static sigc::connection conn;
	m_logMutex.lock();
	std::string str = "";
	if ( showTimestamp ) {
		char timeStamp[32];
		time_t rawTime;
		struct tm *timeInfo;
		time(&rawTime);
		timeInfo = localtime(&rawTime);
		strftime(timeStamp, sizeof(timeStamp), "%H:%M:%S: ", timeInfo);
		str = std::string(timeStamp);
	}
	str = str + msg;
	conn.disconnect();
	conn = m_logDispatcher.connect([this, str]() mutable {
		m_logBuffer->insert(m_logBuffer->end(), str);
		while (gtk_events_pending()) {
			gtk_main_iteration_do(false);
		}
		auto it = m_logBuffer->get_iter_at_line(m_logBuffer->get_line_count());
		m_tvLog->scroll_to(it);
		m_logMutex.unlock();
	});
	m_logDispatcher();
	return;
}

void FrmMain::processFile(const FrameData &frameData) {
	Image a(frameData.m_fileName);
}

void FrmMain::processIfPresent() {
	char buff[512];
	m_queueMutex.lock();
	if ( m_fileQueue.size() == 0 ) {
		m_queueMutex.unlock();
		return;
	}
	FrameData frameData = m_fileQueue.front();
	m_fileQueue.pop();
	m_queueMutex.unlock();
	m_processing.set(true);
	try {
		snprintf(buff, sizeof(buff), "Processing file %s\n", frameData.m_fileName.c_str());
		log(buff);
		processFile(frameData);
		m_nSuccess.set(m_nSuccess.get()+1);
	} catch ( const std::exception& e ) {
		snprintf(buff, sizeof(buff), "Error processing file %s: %s\n",
				frameData.m_fileName.c_str(), e.what());
		log(buff);
		m_nFailure.set(m_nFailure.get()+1);
	} catch (...) {
		snprintf(buff, sizeof(buff), "There was a serious but unknown error processing file %s\n",
				frameData.m_fileName.c_str());
		log(buff);
		m_nFailure.set(m_nFailure.get()+1);
	}
	std::lock_guard<std::mutex> lock(m_queueMutex);
	if ( m_fileQueue.size() == 0 ) {
		// If there are files left in the queue let's pretend were still processing
		m_processing.set(false);
	}
}

void FrmMain::runWorker() {
	m_running.set(true);
	m_shutdown.set(false);
	while ( true ) {
		if ( m_shutdown.get() ) {
			m_running.set(false);
			return;
		}
		processIfPresent();
		std::this_thread::sleep_for(100ms);
	}
}

FrmMain::FrameData::FrameData(const Glib::ustring &fileName, int median, int starCount, double hfr) {
	m_fileName = fileName;
	m_median = median;
	m_starCount = starCount;
	m_hfr = hfr;
}


}
