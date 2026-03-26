package server

import (
	"context"
	"errors"
	"net/http"

	"github.com/antont/gatherers/backend/internal/config"
)

type Server struct {
	cfg config.Config
}

func New(cfg config.Config) *Server {
	return &Server{cfg: cfg}
}

func (s *Server) Handler() http.Handler {
	return http.NewServeMux()
}

func (s *Server) Run(ctx context.Context) error {
	httpServer := &http.Server{
		Addr:    s.cfg.Addr,
		Handler: s.Handler(),
	}

	errCh := make(chan error, 1)
	go func() {
		errCh <- httpServer.ListenAndServe()
	}()

	select {
	case <-ctx.Done():
		shutdownErr := httpServer.Shutdown(context.Background())
		if shutdownErr != nil {
			return shutdownErr
		}
		err := <-errCh
		if errors.Is(err, http.ErrServerClosed) {
			return nil
		}
		return err
	case err := <-errCh:
		if errors.Is(err, http.ErrServerClosed) {
			return nil
		}
		return err
	}
}
