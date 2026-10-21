#pragma once

// Original test-only startup protocol: x0=send handle, x1=receive handle,
// x2=optional private report buffer. No Horizon or Linux ABI is implied.
int RunSyntheticPair(const char* client_path, const char* server_path);
