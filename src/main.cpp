#include "frame.h"

int main(int argc, char *argv[]) {
	Gtk::Main kit(argc, argv);
	ELB::FrmMain *frm = nullptr;
	Glib::RefPtr<Gtk::Builder> builder =
		Gtk::Builder::create_from_file(GLADEDIR "/ekoslightbucket.glade");
	builder->get_widget_derived("main", frm);
	kit.run(*frm);
}
