# Linux systemd deployment

These units run the Linux backend lane:

- `cerious-exchange.service`: local deterministic exchange service on loopback.
- `cerious-gateway.service`: public/backend gateway that serves the terminal and supervises Databento child processes.

Install:

```bash
sudo mkdir -p /etc/cerious
sudo cp deploy/systemd/cerious.env.example /etc/cerious/cerious.env
sudo nano /etc/cerious/cerious.env
sudo cp deploy/systemd/cerious-exchange.service /etc/systemd/system/
sudo cp deploy/systemd/cerious-gateway.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now cerious-exchange
sudo systemctl enable --now cerious-gateway
```

Check:

```bash
systemctl status cerious-exchange
systemctl status cerious-gateway
curl http://127.0.0.1:8011/health
curl http://127.0.0.1:8000/api/health
```

Expose the gateway through TLS/reverse proxy for remote desktop/browser clients.
Do not expose the exchange service directly.
