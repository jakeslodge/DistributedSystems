all: clean server client

clean:
	rm -f controller
	rm -f overseer

server: overseer.c 
	gcc overseer.c -o overseer -pthread

client: controller.c 
	gcc controller.c -o controller -pthread