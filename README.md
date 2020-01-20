# INSA BE Reseau

This project is a simpler implementation of the TCP protocol.

Each version adds a new feature:
V1 is just uses the IP protocls without implementing any feature of the TCP protocol.

V2 adds a complete reliability with a stop & wait mechanism. Each packet as to be send and ackonwledged before the next one is sent.

V3 adds a percentage of acceptable loss. Until a certain percentage of packet loss, no packet is sent again.

V4 lets the application which is sending packets and our protocol work asynchronously, through the use of threads. 
