package server

import (
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
)

type testTarget struct {
	baseURL  string
	external bool
	closeFn  func()
}

func newTestTarget(t *testing.T, handler http.Handler) testTarget {
	t.Helper()

	if baseURL := os.Getenv("GATHERERS_BACKEND_BASE_URL"); baseURL != "" {
		return testTarget{
			baseURL:  strings.TrimRight(baseURL, "/"),
			external: true,
			closeFn:  func() {},
		}
	}

	server := httptest.NewServer(handler)
	return testTarget{
		baseURL:  server.URL,
		external: false,
		closeFn:  server.Close,
	}
}
