create table `user` (
	user_id int(8) not null auto_increment,
	created timestamp not null default current_timestamp,
	email varchar(250) not null,
	zip varchar(5) not null,
	schedule time not null default '6:00',
	confirm_code varchar(50) null,
	next_send timestamp null,
	last_sent timestamp null,
	unique key user_id(user_id, email)
);
