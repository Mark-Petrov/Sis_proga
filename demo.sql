DROP DATABASE demo;
CREATE DATABASE demo;
USE demo;

CREATE TABLE products (id INT INDEXED NOT NULL, title STRING NOT NULL, price INT DEFAULT 0, note STRING);

INSERT INTO products (id, title, price) VALUES (1, "Milk", 89), (2, "Bread", 45), (3, "Cheese", 320);

SELECT * FROM products;

SELECT * FROM products WHERE id = 2;

INSERT INTO products (id, title) VALUE (4, "Water");
SELECT * FROM products WHERE id = 4;

SELECT title, price FROM products WHERE price BETWEEN 40 AND 100;
SELECT title FROM products WHERE price > 50 AND title LIKE "M%";

SELECT COUNT(*) FROM products;
SELECT SUM(price) AS total FROM products;
SELECT AVG(price) AS avg_price FROM products;

UPDATE products SET price = 99 WHERE id = 1;
SELECT * FROM products WHERE id = 1;

DELETE FROM products WHERE id = 3;
SELECT * FROM products;
