function install() {
	echo == executing install on host: $HOSTNAME ==
	sudo cp ~/src/xtree/ctests/weather/weather /usr/local/bin/
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
	/usr/local/bin/weather -z 10974 -m aaa -t
}

function build() {
	echo == executing build on host: $HOSTNAME ==
	cd ~/src/xtree/ctests
	git pull
	cd weather
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

function clone_ctests() {
	host=$1
	ssh -t $host "mkdir -p ~/src/xtree"
	ssh -t $host "cd ~/src/xtree; git clone https://github.com/serge-v/ctests"
}
