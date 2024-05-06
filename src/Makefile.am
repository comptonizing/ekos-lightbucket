bin_PROGRAMS=ekoslightbucket

gui.h: ekoslightbucket.glade Makefile
	echo -e "#include <iostream>\nconst std::string __guiData = R\"(" > $@
	cat $< >> $@
	echo ")\";" >> $@

ekoslightbucket_SOURCES=gui.h main.cpp frame.h frame.cpp image.h image.cpp
