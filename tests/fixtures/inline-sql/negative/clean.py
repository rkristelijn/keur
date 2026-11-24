# negative: loaded from a .sql file, ORM call, prose
query = load_sql("tasks/select_open.sql")
tasks = Task.objects.filter(owner=uid)
msg = "Please select an option from the menu."
