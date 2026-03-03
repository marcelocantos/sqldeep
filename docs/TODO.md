# TODO

## Syntax enhancements

- [x] **Allow ON and USING clauses after `a->b` syntax**
  Override the default FK convention when the column name doesn't match
  `<parent_table>_id`. E.g., `c->orders ON person_id` or
  `c->orders USING (cust_id)`.
