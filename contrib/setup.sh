#!/bin/sh

sudo ip addr add 10.0.1.1/24 dev tun0
sudo ip addr add 10.0.2.1/24 dev tun1
sudo ip link set dev tun0 up
sudo ip link set dev tun1 up

./ttun tun0 10.99.0.1 2090 127.0.0.1 2090 &
./ttun tun1 127.0.0.1 2090 10.99.0.1 2090 &
