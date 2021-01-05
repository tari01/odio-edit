CC = gcc

ifndef O
	O = 3
endif

CFLAGS = -g -O$O -Wall $(shell pkg-config --cflags gtk+-3.0 gstreamer-1.0)
LDFLAGS += $(shell pkg-config --libs gtk+-3.0 gstreamer-1.0 gstreamer-audio-1.0 gstreamer-app-1.0) -lpthread -lm -lodiosacd
VPATH = src
OBJECTS = gstreamer chunk chunkview main message mainwindow ringbuf player file viewcache datasource tempfile document

.PHONY: all clean install uninstall trans
all: clean $(OBJECTS) odio-edit trans
gstreamer: gstreamer.c; $(CC) -c $(CFLAGS) $^ -o $(VPATH)/$@.o
chunk: chunk.c; $(CC) -c $(CFLAGS) $^ -o $(VPATH)/$@.o
chunkview: chunkview.c; $(CC) -c $(CFLAGS) $^ -o $(VPATH)/$@.o
main: main.c; $(CC) -c $(CFLAGS) $^ -o $(VPATH)/$@.o
message: message.c; $(CC) -c $(CFLAGS) $^ -o $(VPATH)/$@.o
mainwindow: mainwindow.c; $(CC) -c $(CFLAGS) $^ -o $(VPATH)/$@.o
ringbuf: ringbuf.c; $(CC) -c $(CFLAGS) $^ -o $(VPATH)/$@.o
player: player.c; $(CC) -c $(CFLAGS) $^ -o $(VPATH)/$@.o
file: file.c; $(CC) -c $(CFLAGS) $^ -o $(VPATH)/$@.o
viewcache: viewcache.c; $(CC) -c $(CFLAGS) $^ -o $(VPATH)/$@.o
datasource: datasource.c; $(CC) -c $(CFLAGS) $^ -o $(VPATH)/$@.o
tempfile: tempfile.c; $(CC) -c $(CFLAGS) $^ -o $(VPATH)/$@.o
document: document.c; $(CC) -c $(CFLAGS) $^ -o $(VPATH)/$@.o
odio-edit: $(foreach object, $(OBJECTS), $(VPATH)/$(object).o); $(CC) -o data/usr/bin/$@ $^ $(LDFLAGS)

clean:

	$(shell rm -f ./data/usr/bin/odio-edit)
	$(shell rm -f $(foreach librarydir, $(VPATH), $(librarydir)/*.o))
	$(shell rm -f -r ./data/usr/share/locale)

install:

	$(shell install -d $(DESTDIR)/usr/bin)
	$(shell install ./data/usr/bin/odio-edit $(DESTDIR)/usr/bin)
	$(shell install -d $(DESTDIR)/usr/share/applications)
	$(shell install ./data/usr/share/applications/odio-edit.desktop $(DESTDIR)/usr/share/applications)
	$(shell install -d $(DESTDIR)/usr/share/icons/hicolor/scalable/apps)
	$(shell install ./data/usr/share/icons/hicolor/scalable/apps/odio-edit.svg $(DESTDIR)/usr/share/icons/hicolor/scalable/apps)
	$(shell install -d $(DESTDIR)/usr/share/glib-2.0/schemas)
	$(shell install ./data/usr/share/glib-2.0/schemas/in.tari.odio-edit.gschema.xml $(DESTDIR)/usr/share/glib-2.0/schemas)
	$(foreach f, $(wildcard ./po/*.po), $(shell install -d $(DESTDIR)/usr/share/locale/$(notdir $(basename $f))/LC_MESSAGES))
	$(foreach f, $(wildcard ./po/*.po), $(shell install ./data/usr/share/locale/$(notdir $(basename $f))/LC_MESSAGES/odio-edit.mo $(DESTDIR)/usr/share/locale/$(notdir $(basename $f))/LC_MESSAGES))

ifndef DESTDIR
	$(shell gtk-update-icon-cache /usr/share/icons/hicolor)
	$(shell glib-compile-schemas /usr/share/glib-2.0/schemas)
endif

uninstall:

	$(shell rm -f $(DESTDIR)/usr/bin/odio-edit)
	$(shell rm -f $(DESTDIR)/usr/share/applications/odio-edit.desktop)
	$(shell rm -f $(DESTDIR)/usr/share/glib-2.0/schemas/in.tari.odio-edit.gschema.xml)
	$(shell rm -f $(DESTDIR)/usr/share/icons/hicolor/scalable/apps/odio-edit.svg)
	$(foreach f, $(wildcard ./po/*.po), $(shell rm -f $(DESTDIR)/usr/share/locale/$(notdir $(basename $f))/LC_MESSAGES/odio-edit.mo))

trans:

	$(shell xgettext --force-po --add-comments=" Translators:" -o ./po/odio-edit.pot --no-wrap --copyright-holder "2019-2021 Robert Tari" --package-name odio-edit -d odio-edit --msgid-bugs-address "https://github.com/tari01/odio-edit/issues" -L ObjectiveC $(VPATH)/*.h* $(VPATH)/*.c*)
	$(shell sed -i '/"Content-Type: text\/plain; charset=CHARSET\\n"/c\"Content-Type: text\/plain; charset=UTF-8\\n"' ./po/odio-edit.pot)
	$(foreach f, $(wildcard ./po/*.po), $(shell msgmerge --silent --update --no-fuzzy-matching --backup=off $f ./po/odio-edit.pot))
	$(foreach f, $(wildcard ./po/*.po), $(shell mkdir -p ./data/usr/share/locale/$(notdir $(basename $f))/LC_MESSAGES))
	$(foreach f, $(wildcard ./po/*.po), $(shell msgfmt -c -o ./data/usr/share/locale/$(notdir $(basename $f))/LC_MESSAGES/odio-edit.mo $f))
