
both: server.c client.c
	gcc server.c list.c -o server
	gcc client.c -o client

server: server.c
	gcc server.c list.c -o server
