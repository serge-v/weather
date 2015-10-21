create table `user` (
	user_id int(8) not null auto_increment,
	created date not null,
	email varchar(250) not null,
	zip varchar(5) not null,
	next_send date default null,
	last_sent date default null,
	unique key user_id(`user_id`, `email`)
);
