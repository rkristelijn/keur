# positive: simple statements in Python
cur.execute("SELECT id, name FROM users WHERE id = %s", (uid,))
cur.execute("insert into audit(event) values (?)", (e,))
Q = """
    SELECT id
    FROM tasks
"""
