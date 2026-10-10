# e2e fixture for `keur analyze literals` — one literal per bucket.
# identity: an email, a long numeric id, a url
owner = "alice@example.com"
account = "2650567"
endpoint = "https://api.example.com/v1"
# magic: the same WORD appears 3x (hits rule-of-three threshold)
state_a = "PENDING"
state_b = "PENDING"
state_c = "PENDING"
# noise: a language keyword and a tiny number (must NOT be flagged)
flag = "true"
retries = "3"
