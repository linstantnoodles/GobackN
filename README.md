###UDP with GBN Implementation

####Compilation and execution

#####Server

gcc -o a.out server.c DieWithError.c -lpthread

./a.out [port]

#####Client

gcc -o a.out client.c -lpthread

./a.out [host] [port]


