set -e
source deploy-funcs.sh

host=`cat ~/.config/departures/host.txt`
callcmd="cd \${HOME}/src/xtree/ctests/weather; source ./deploy-funcs.sh; "

if [ x$1 == "x-l" ]; then
	ssh -t $host "$callcmd install"
elif [ x$1 == "x-i" ]; then
	ssh -t $host "$callcmd info"
elif [ x$1 == "x-c" ]; then
	shift
	ssh -t $host "
		echo == executing command on host: \$HOSTNAME ==
		$*"
elif [ x$1 == "x-t" ]; then
	ssh -t $host "$callcmd test"
elif [ x$1 == "x-b" ]; then
	ssh -t $host "$callcmd build"
elif [ x$1 == "x-u" ]; then
	ssh -t $host "mkdir -p ~/src/xtree"
	ssh -t $host "cd ~/src/xtree; git clone https://github.com/serge-v/ctests"
else
	echo usage: deploy.h [-bitl]
	echo '    ' -i       info
	echo '    ' -u       clone
	echo '    ' -b       build
	echo '    ' -l       install
	echo '    ' -t       test
	echo '    ' -c       run command on remote server
fi
