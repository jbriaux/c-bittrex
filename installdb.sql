CREATE DATABASE bbot;
USE bbot;
CREATE TABLE Orders (
       OrderID int AUTO_INCREMENT,
       UUID varchar(40) NOT NULL UNIQUE,
       Market varchar(7),
       Quantity double,
       Rate double,
       BotType ENUM('buy','sell'),
       BotState ENUM('pending','processed'),
       PRIMARY KEY ( OrderID )
);
GRANT ALL PRIVILEGES ON bbot.* TO 'bbot_user'@'localhost' IDENTIFIED BY 'Whr3PvCJ7cb';
FLUSH PRIVILEGES;
