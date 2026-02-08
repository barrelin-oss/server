# Forum Authentication - Client Integration Guide

## Overview

The server supports an optional forum-based authentication mode where credentials are verified against an external forum database (MySQL) via PHP endpoints. When enabled, the server delegates password verification to the forum and issues tokens for auto-login.

## Protocol Changes

### Login Request

The `login_request` message now accepts either `password` or `forum_token` (at least one required):

```json
// Password-based login (first time or when token expired)
{
    "type": "login_request",
    "seq": 1,
    "data": {
        "username": "user@example.com",
        "password": "their_password"
    }
}

// Token-based auto-login (stored from previous session)
{
    "type": "login_request",
    "seq": 1,
    "data": {
        "username": "user@example.com",
        "forum_token": "a1b2c3d4e5f6..."
    }
}
```

### Login Response

On success, the response now optionally includes a `forum_token` field (only present on password-based forum login):

```json
{
    "type": "login_response",
    "seq": 1,
    "data": {
        "success": true,
        "session_token": "game_session_abc123...",
        "forum_token": "64_char_hex_token_for_auto_login..."
    }
}
```

### Error Responses

New error code: `forum_auth_failed` — returned when:
- Invalid username/password against forum
- Invalid/expired token
- Password was changed on the forum (all tokens revoked)

```json
{
    "type": "login_response",
    "seq": 1,
    "data": {
        "success": false,
        "error": "forum_auth_failed"
    }
}
```

## Client-Side Implementation

### Storage

Store the following locally (e.g., in a config file or OS credential store):

| Key | Value | Notes |
|-----|-------|-------|
| `username` | User's email/username | Always stored |
| `forum_token` | 64-char hex string | From login response, used for auto-login |

Do **not** store the plaintext password.

### Login Flow

```
1. Check for stored forum_token
   |
   +-- YES: Send login_request with {username, forum_token}
   |         |
   |         +-- Success: Store session_token, proceed to character select
   |         |
   |         +-- "forum_auth_failed": Token expired/revoked
   |             -> Clear stored forum_token
   |             -> Fall through to password prompt
   |
   +-- NO: Show password prompt
            |
            Send login_request with {username, password}
            |
            +-- Success: Store session_token AND forum_token (from response)
            |            -> Proceed to character select
            |
            +-- "forum_auth_failed": Wrong credentials
                -> Show error, re-prompt
```

### Token Lifecycle

- Tokens expire after **30 days** (server-configurable)
- Tokens are **automatically revoked** if the user changes their forum password
- On `forum_auth_failed`, always clear the stored token and prompt for password
- A new token is issued on every successful password-based login

### Backward Compatibility

When the server is NOT configured for forum auth (the default), it uses local PostgreSQL credentials:
- Send `login_request` with `{username, password}` as before
- No `forum_token` will be present in the response
- The client should handle the absence of `forum_token` gracefully (just don't store anything)

### Example Client Code (Pseudocode)

```cpp
void attempt_login()
{
    json request;
    request["username"] = stored_username;

    if (!stored_forum_token.empty())
    {
        // Try auto-login with token
        request["forum_token"] = stored_forum_token;
    }
    else
    {
        // Prompt user for password
        request["password"] = prompt_password();
    }

    send_login_request(request);
}

void on_login_response(const json& response)
{
    if (response["success"])
    {
        session_token = response["session_token"];

        // Store forum token for next session (if present)
        if (response.contains("forum_token"))
        {
            stored_forum_token = response["forum_token"];
            save_credentials_to_disk();
        }

        proceed_to_character_select();
    }
    else
    {
        if (response["error"] == "forum_auth_failed" && !stored_forum_token.empty())
        {
            // Token expired or revoked - clear and re-prompt
            stored_forum_token.clear();
            save_credentials_to_disk();
            show_login_prompt("Session expired. Please log in again.");
        }
        else
        {
            show_error(response["error"]);
        }
    }
}
```

## Server Configuration

Add to `GServer.cfg` (YAML format):

```yaml
forum_auth:
    enabled: true
    login_url: "https://yourforum.com/api/auth_login.php"
    validate_url: "https://yourforum.com/api/auth_validate.php"
    api_key: "your_secret_api_key"
```

When `enabled: false` (default), the server uses local PostgreSQL password verification. The `password` field in login requests is verified against Argon2 hashes stored in the `accounts` table.
