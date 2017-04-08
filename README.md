# avr-enc28j60
A library for the enc28j60 that supports streaming network packets.

This libaray focuses on TCP connections and on streaming the content of the network packets.
It allows you to read from the current TCP package as a stream or to write to the response package.
That way, you don't need to allocate the RAM to store a whole TCP package.

## Using

Add this to your main:

```
initTcpIp();
initEnc();

// <Add any other init code here, e.g. register ports>

// Good to have a WDT running
wdt_reset();
wdt_enable(WDTO_1S);

// If you have interrupts, use them.
sei();

while (1) {
	wdt_reset();
	pollEnc();
	tcpTimeoutPoll();
	
	// <Add any other polling / main loop code you want>
}
```

TCP Timeouts:
Call `tcpTimeoutDowncount()` every second. You can use a timer for this.

The call to `tcpTimeoutPoll()` handles the timeouts.

## Adding a TCP server

```
typedef struct {
	TCPChannel channel;
	uint16_t mydata; // add as much as you want
} MyAppSession;



TCPChannel* myapp_connect {
	// Allocate a new TCPChannel object or null to not connect.
	if (... have free connection resources ...) {
		MyAppSession *session = ...;
		return (TCPChannel*) session;
	} else {
		return NULL;
	}
}

void myapp_receive(TCPChannel *channel) {
	MyAppSession *session = (MyAppSession*) channel;
	if (encGetRemaining() == 0) {
		// we only received an ack response.
		// If you want to send anything, do it ;-)
	} else {
		// read the first word from the TCP stream
		char buffer[12];
		encReadUntil((uint8_t*) buffer, sizeof(buffer), ' ');
		// alternative:
		encReadUntilSpace((uint8_t*) buffer, sizeof(buffer));
		// read an integer
		encReadInt();

		// Starts a new TCP package
		sendTcpResponseHeader((TCPChannel *) session, (1 << TCP_FLAG_PSH));
		// Write an integer
		encWriteInt32(12345);
		encWriteChar(' ');
		// Respond with a integer format string
		uint16_t parameters = {1, 2};
		encWriteStringParameters_P(message, parameters, 2);
		encWriteChar('\n');
		// Sends the package.
		sendTcpResponse((TCPChannel*) session);
	}
}

void myapp_disconnect(TCPChannel *channel) {
        MyAppSession *session = (MyAppSession*) channel;

        // You can send a last response if you want to

	// Mark the channel as free / free it to not leak resources.
}


TCPApp myApp = {
	// Port
	80,
	// Called whenever a TCP connection is established.
	myapp_connect,
	// Called for every received package, including ack packages
	myapp_receive,
	// Called on disconnect
	myapp_disconnect
};
```

In your init:
```
addTcpApp(&myApp);
```

