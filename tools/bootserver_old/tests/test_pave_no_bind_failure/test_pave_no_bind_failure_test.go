package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/bootserver_old/tests"
)

func TestPaveNoBindFailure(t *testing.T) {
	_, cleanup := bootserver.StartQemu(t, "netsvc.all-features=true, netsvc.netboot=false", "full")
	defer cleanup()

	// Test that advertise request is NOT serviced and paving does NOT start
	// as netsvc.netboot=false
	bootserver.AttemptPaveNoBind(t, false)
}
