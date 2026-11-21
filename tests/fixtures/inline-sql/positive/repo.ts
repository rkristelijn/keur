// positive: TypeScript
const q = 'SELECT * FROM tasks WHERE owner = $1';
await db.query(`DELETE FROM tasks WHERE id = ${id}`);
