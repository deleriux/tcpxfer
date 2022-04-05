tcpxfer
=======

Run a constant throughnput analysis between two hosts and report significant deviations from expected performance.

This is client project that was used to guarantee an expected SLA between two endpoints.

This program constantly sends both ways a configured throughput and collects tcp performance stats into a sliding window.

We maintain an ongoing state against a constantly sliding window.

In that case, we would report the issue to the client.

# Purpose

This was used successfully to improve line performance with the vendor, it effectively pays for itself by constantly monitoring any deviation from the agreed SLA that this client has and giving the customer an opportunity to claim any fees on their SLA.

The vendor has since altered their SLA claims.. :-)

Requirements
------------

This is written with libev and libgsl requirements.

# Implementation

The stats are correlated using the pearson coefficient, what we're actually trying to measure is the gradient of the pearson correlation over the X axis. That is -- how 'horizontal' the line is.

Typically on a standard, non-congested throughput of TCP this line should be horizontal. If the link is congested this results in a 'sawtooth' TCP tran
sfer pattern and our line deviates from horizontal by a particular angle. When the angle is too many radians out of tolerance we alert.

# Performance

It is a singularly threaded program. Multithreading this really isn't going to help much even given doing 10gbps throughput tests, primarily because you are ultimately stuffing this into a DMA bucket somewhere and firing an interrupt to the device.

The device itself might have multiple ring buffers (not unusual on high performance devices) and multiple hardware contexts to process the packets but thats a free-of-charge cost you dont have to implement for.

An interesting quirk I did find is that if you want to do really high throughputs (~1gbps+) you need to measure the timerfd overruns as its trivial to miss cycles and thus not drive the proper packet generation at the correct intervals.
