# 8AX VPS Admin API

This is the VPS-side endpoint used by the Factory Client `OTA发布` and `驱动发布` tabs.

It implements:

- `GET /healthz`
- `POST /api/v1/admin/ota/packages`
- `POST /api/v1/admin/drive-profiles`

The OTA publish endpoint accepts the Factory Client multipart upload and verifies package and signature SHA256/size. Public OTA packages are written under `/opt/8ax-auth/storage/ota/public/<product>/<channel>/`; private OTA packages are written under `/opt/8ax-auth/storage/private/<vpsDistributionId>-<pl_dna_hash>/ota/<product>/<channel>/`. The static mirror keeps the same scope shape under `/var/www/html/updates/ota`, with private paths using `/var/www/html/updates/ota/private/<vpsDistributionId>-<pl_dna_hash>/ota/<product>/<channel>/`.

The drive profile publish endpoint accepts a `driver_profile_map.json`, verifies SHA256/size and scope, then writes public profiles under `/opt/8ax-auth/storage/drive-profiles/public` and private profiles under `/opt/8ax-auth/storage/private/<vpsDistributionId>-<pl_dna_hash>/driver_profile_map.json`. Private device folders must use the VPS-verified id-dna folder; hash-only and old ID-only private folders are not supported publish targets. `plDnaHash` is submitted for VPS-side binding validation and service-side path resolution, but device download URLs still carry only the 6-digit ID. The same device private folder also owns `device_authorization.json`, `ota/`, and future `winremote-uploads/`.

It does not generate signatures, hold the factory signing key, select board OTA scope, start board upgrades, write board state, or reboot anything.

For private OTA/profile publishing, the service must validate `privateId` and `privateHash` against the VPS `devices` table before writing files. A missing device, mismatched DNA hash, or unavailable database check fails closed and writes nothing.

Run on VPS:

```bash
export AX8_VPS_ADMIN_USER='<admin-user>'
export AX8_VPS_ADMIN_PASSWORD='<admin-password>'
python3 /opt/8ax-auth/vps-admin-api/vps_ota_admin_api.py --host 127.0.0.1 --port 18081
```

The deployed service is `8ax-ota-admin-api.service`; it loads `/opt/8ax-auth/secrets/admin-auth.env` and accepts either `AX8_VPS_ADMIN_USER` / `AX8_VPS_ADMIN_PASSWORD` or the existing `AX8_ADMIN_USER` / `AX8_ADMIN_PASSWORD` names.

Optional paths:

```bash
python3 vps_ota_admin_api.py \
  --storage-root /opt/8ax-auth/storage/ota \
  --static-root /var/www/html/updates/ota \
  --drive-storage-root /opt/8ax-auth/storage/drive-profiles \
  --drive-static-root /var/www/html/updates/drive-profiles \
  --drive-legacy-static-root /var/www/html/drive-profiles \
  --private-root /opt/8ax-auth/storage/private
```

Factory Client may connect through an admin tunnel that forwards local `127.0.0.1:18081` to VPS `127.0.0.1:18081`. Device registration and read-only admin lists remain on the gateway port `18080`; OTA/profile publishing uses the admin API port `18081`.
