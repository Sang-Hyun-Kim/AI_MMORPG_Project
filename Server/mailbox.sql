DROP TABLE IF EXISTS `Player`;
CREATE TABLE `Player` (
  `PlayerId` bigint(20) NOT NULL AUTO_INCREMENT,
  `Name` varchar(50) NOT NULL,
  `Level` int(11) NOT NULL DEFAULT '1',
  `Exp` int(11) NOT NULL DEFAULT '0',
  `Hp` int(11) NOT NULL DEFAULT '100',
  `ClassId` int(11) NOT NULL DEFAULT '0',
  `Gold` int(11) NOT NULL DEFAULT '0',
  `PosX` float NOT NULL DEFAULT '0',
  `PosY` float NOT NULL DEFAULT '0',
  `PosZ` float NOT NULL DEFAULT '0',
  `Yaw` float NOT NULL DEFAULT '0',
  PRIMARY KEY (`PlayerId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS `Mailbox`;
CREATE TABLE `Mailbox` (
  `MailId` bigint(20) NOT NULL AUTO_INCREMENT,
  `PlayerId` bigint(20) NOT NULL,
  PRIMARY KEY (`MailId`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO Player (PlayerId, Name) VALUES (0, 'DummyPlayer');
