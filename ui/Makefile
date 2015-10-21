all: weatherui

weatherui:
	make -C ~/b/weatheruib

clean:
	make -C ~/b/weatheruib clean

debug: weatherui
	cgdb ~/b/weatheruib/weatherui

val: weatherui
	valgrind --leak-check=full ~/b/weatheruib/weatherui

deploy: weatherui
	cp ~/b/weatheruib/weatherui /usr/lib/cgi-bin/weatherui

ver:
	@echo == built ==
	@~/b/weatheruib/weatherui -v
	@echo == deployed ==
	@/usr/lib/cgi-bin/weatherui -v
