#!/bin/sh
# positive: 8 else-if branches on string literals (KEUR-QUAL-001 and 001b)
if [ "$1" = "id" ]; then echo 0
elif [ "$1" = "title" ]; then echo 1
elif [ "$1" = "kind" ]; then echo 2
elif [ "$1" = "state" ]; then echo 3
elif [ "$1" = "owner" ]; then echo 4
elif [ "$1" = "due" ]; then echo 5
elif [ "$1" = "tags" ]; then echo 6
elif [ "$1" = "url" ]; then echo 7
elif [ "$1" = "rank" ]; then echo 8
fi
