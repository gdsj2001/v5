# 8ax Dealer Client

This is the Windows desktop client for 8ax dealers.

Current scope:

- Connect to the 8ax authorization server.
- Register the dealer main account in the Windows client through `/api/v1/dealer/register`.
- Check client online update status through `/api/v1/dealer-client/update-check`.
- Login through `/api/v1/dealer-user/login`.
- Show `dealer_daily_code` through `/api/v1/dealer/daily-code` when the account is allowed.
- Copy the employee registration link returned by the server.

The first deployed server phase currently only exposes `/healthz` and static dealer registration page routing, so business APIs may return `501` until the dealer API is implemented on `vps3-dmit`.

## Build

Prerequisite: .NET 9 SDK.

```powershell
dotnet build .\8ax.DealerClient\8ax.DealerClient.csproj
```

## Publish

```powershell
dotnet publish .\8ax.DealerClient\8ax.DealerClient.csproj -c Release -r win-x64 --self-contained true /p:PublishSingleFile=true /p:EnableCompressionInSingleFile=true
```

The published executable should later be signed and uploaded to the server as a dealer-client update package with manifest, SHA-256, and signature.
