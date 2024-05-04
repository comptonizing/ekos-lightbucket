#pragma once

#include <iostream>
#include <queue>
#include <mutex>
#include <thread>
#include <chrono>
#include <ctime>
#include <fstream>
#include <cstdio>

#include <gtkmm.h>
#include <glib.h>

#include "common.h"
#include "image.h"

#define RED_BEGIN "\033[1;31m"
#define RED_END "\033[0m"

namespace ELB {

	using namespace std::chrono_literals;

	class FrmMain : public Gtk::ApplicationWindow {
			class FrameData {
				public:
					FrameData(const Glib::ustring &fileName, int median, int starCount, double hfr);
					~FrameData() = default;
					Glib::ustring m_fileName;
					int m_median;
					int m_starCount;
					double m_hfr;
			};

		public:
			~FrmMain();
			FrmMain(BaseObjectType* cobject, const Glib::RefPtr<Gtk::Builder>& refGlade);
		private:
			void showError(Glib::ustring title, Glib::ustring message, Glib::ustring secondaryMessage = "");
			void showQueueWarning(int nItems);
			void onCaptureComplete(
					const Glib::RefPtr<Gio::DBus::Connection>& connection,
					const Glib::ustring &sender_name,
					const Glib::ustring &object_path,
					const Glib::ustring &interface_name,
					const Glib::ustring &signal_name,
					const Glib::VariantContainerBase& parameters
					);
			void log(const std::string &msg, bool showTimestamp = true);
			void processFile(const FrameData &frameData);
			void processIfPresent();
			void runWorker();
			void stopWorker();
			bool quit(_GdkEventAny* event);
			void help();
			void initConfig();
			void saveConfig();

			std::string m_kstarsName = "org.kde.kstars";
			std::string m_capturePath = "/KStars/Ekos/Capture";
			std::string m_captureInterface = "org.kde.kstars.Ekos.Capture";
			std::string m_signalName = "captureComplete";
			std::string m_configFile;
			Glib::RefPtr<Gtk::Builder> builder;
			Glib::RefPtr<Gio::DBus::Connection> m_dbus;
			Glib::RefPtr<Gio::DBus::Proxy> m_proxy;
			Glib::RefPtr<Gtk::TextBuffer> m_logBuffer;
			std::unique_ptr<Gtk::MessageDialog> m_dialog;
			std::unique_ptr<Gtk::MessageDialog> m_finishDialog;
			Gtk::Label *m_lblEkosStatus;
			Gtk::Entry *m_entryUser, *m_entryKey;
			Gtk::TextView *m_tvLog;
			Gtk::Label *m_labelQueueSize, *m_labelSuccessSize, *m_labelFailureSize, *m_labelProcessing;
			Gtk::Spinner *m_spinnerProcessing;
			Gtk::Button *m_buttonHelp, *m_buttonSave;
			Glib::Dispatcher m_logDispatcher;

			std::queue<FrameData> m_fileQueue;
			std::mutex m_logMutex;
			std::mutex m_queueMutex;
			SerialProperty<bool> m_shutdown = false;
			SerialProperty<bool> m_running = false;
			SerialProperty<bool> m_processing = false;
			SerialProperty<size_t> m_nSuccess = 0;
			SerialProperty<size_t> m_nFailure = 0;
			std::thread m_workerThread;
	};
}
