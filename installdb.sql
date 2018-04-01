CREATE DATABASE bbot;
USE bbot;
CREATE TABLE Orders (
       OrderID int AUTO_INCREMENT,
       UUID varchar(40) NOT NULL UNIQUE,
       Market varchar(8),
       Quantity DECIMAL(16,8),
       Rate DECIMAL(16,8),
       BotType ENUM('buy','sell'),
       BotState ENUM('pending','processed', 'cancelled'),
       Btc  DECIMAL(16,8),
       Gain DECIMAL(16,8),
       PRIMARY KEY ( OrderID )
);
GRANT ALL PRIVILEGES ON bbot.* TO 'bbot_user'@'localhost' IDENTIFIED BY 'Whr3PvCJ7cb';
FLUSH PRIVILEGES;
