###UDP with GBN Implementation

####Compilation and execution

#####Server

gcc -o a.out server.c DieWithError.c -lpthread

./a.out [port]

#####Client

gcc -o a.out client.c -lpthread

./a.out [host] [port]

####Report

Normal Send Time: 26.6 seconds
Random # Send : 33.2 seconds with 10 second timeout.

*This includes the 1 second delay from the sender.*
####Notes

I have three threads running on both the sender and the receiver (application, receiver, and sender).

On the send side, my application communicates with the sender by creating packets and pushing them into a circular buffer. This sets conditional flags that will cause the sender to start grabbing packets from the buffer and sending them off. The sender will send as long as the number that could be sent > the number already sent and unacked.

The receiver communicates with the application using a conditional flag *can_send*, which is initially set to the window size following the handshake. As long as the the number of packets already pushed by the application to the transport layer (sender) does not exceed the number it can send (as signaled by the receiver), it may push new packets to the buffer. 

The receive side also uses conditional variables for coordination. The only difference is that the receiver communicates to both the sender and the application. If there is a new (and valid) packet in the buffer, the sender sends an ACK and the application writes it to a file.

Extra comments:

* I moved the handshake on the client to its own method
* I wrote a method called createPacket to construct a packet
* I wrote a method called slideDistance to figure out how far I need to slide my window given a certain sequence number. For example, If I send out packets with sequence numbers 5,6,7,0 and I get back 0, it should move my window by 4 and signal to my application to push more packets.


