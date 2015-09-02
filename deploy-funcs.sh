function install() {
	echo == executing install on host: $HOSTNAME ==
	sudo cp ~/src/xtree/weather/weather /usr/local/bin/
	/usr/local/bin/weather -v
}

function info() {
	echo == executing info on host: $HOSTNAME ==
	crontab -l | grep weather
	/usr/local/bin/weather -v
}

function test() {
	echo == executing test on host: $HOSTNAME ==
	crontab -l | grep weather
	/usr/local/bin/weather -v
	/usr/local/bin/weather -z 10974 -m serge0x76+weather@gmail.com -t
}

function build() {
	echo == executing build on host: $HOSTNAME ==
	cd ~/src/xtree/weather
	git pull
	git submodule update
	cmake .
	make clean
	make
	echo == new version ==
	./weather -v
	echo == deployed version ==
	/usr/local/bin/weather -v
	echo == crontab ==
	crontab -l | grep weather
	echo
	echo Project was build on $HOSTNAME. For install run script with -l parameter.
}
