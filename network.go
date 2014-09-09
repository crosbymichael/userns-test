package main

import (
	"flag"
	"log"
	"net"
	"strconv"

	"github.com/docker/libcontainer/netlink"
)

func init() {
	flag.Parse()
}

func main() {
	pid, err := strconv.Atoi(flag.Arg(0))
	if err != nil {
		log.Fatal(err)
	}

	iface, err := net.InterfaceByName("eth0")
	if err != nil {
		log.Fatal(err)
	}
	if err := netlink.NetworkSetNsPid(iface, pid); err != nil {
		log.Fatal(err)
	}
}
