export PATH=/opt/mysql/bin:$PATH

mysqldump weather --no-data -u dbuser --password=`cat password~.txt` > db_schema.txt
