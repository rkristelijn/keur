# positive: 8 else-if branches on string literals (KEUR-QUAL-001 and 001b)
def set_field(r, key, v):
    if key == "id":
        r.id = v
    elif key == "title":
        r.title = v
    elif key == "kind":
        r.kind = v
    elif key == "state":
        r.state = v
    elif key == "owner":
        r.owner = v
    elif key == "due":
        r.due = v
    elif key == "tags":
        r.tags = v
    elif key == "url":
        r.url = v
    elif key == "rank":
        r.rank = v
