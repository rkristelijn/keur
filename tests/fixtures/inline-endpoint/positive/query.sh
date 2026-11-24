#!/bin/sh
# positive: shell
curl -s "https://api.clickup.com/api/v2/team/$TEAM/task?page=0"
URL="$BASE/rest/api/content?spaceKey=$SPACE"
