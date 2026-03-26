package main

import (
	"context"
	"log"
	"os/signal"
	"syscall"

	"github.com/antont/gatherers/backend/internal/config"
	"github.com/antont/gatherers/backend/internal/server"
)

func main() {
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	cfg := config.FromEnv()
	srv := server.New(cfg)

	log.Printf("starting gatherers backend skeleton on %s", cfg.Addr)
	if err := srv.Run(ctx); err != nil {
		log.Fatal(err)
	}
}
