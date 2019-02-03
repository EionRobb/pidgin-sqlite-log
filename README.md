pidgin-sqlite-log
=================

SQLite Logging for Pidgin/Finch/libpurple

Windows downloads in the [releases area](https://github.com/EionRobb/pidgin-sqlite-log/releases)

To Enable
=========
To use SQLite logging, open Tools->Plugins and enable the 'SQLite Logging' plugin:
![image](https://user-images.githubusercontent.com/1063865/52181597-75cb4680-2858-11e9-9ab5-f81a0d52dd5e.png)

Then enable the logging plugin, then in Tools->Preferences->Logging, set the "log format" to SQLite:
![image](https://user-images.githubusercontent.com/1063865/52181610-824f9f00-2858-11e9-81cf-f9771a560c57.png)

The SQLite log file will be created in your [.purple/logs folder](https://developer.pidgin.im/wiki/ConfigurationFiles)

Settings
========
The only setting in the plugin is to "Group conversation history by date", which will show conversation history on a day-by-day basis instead of a conversation-by-conversation basis (which is what the HTML/Text logger does)

Data Structure
==============
The SQLite tables are intentionally simple:

***accounts***

| Field       | Description            | Type                 |
|-------------|------------------------|----------------------|
| id          | Primary Key            | autoincrementing int |
| username    | The account username   | varchar              |
| protocol_id | The protocol plugin id | varchar              |


***logs***

| Field      | Description                                                             | Type                 |
|------------|-------------------------------------------------------------------------|----------------------|
| id         | Primary Key                                                             | autoincrementing int |
| account_id | accounts.id foreign key                                                 | int                  |
| type       | The conversation type - 0 = IM, 1 = Chat, 2 = System                    | int                  |
| name       | The conversation name - eg EionRobb for an IM, #pidgin for a group chat | varchar              |
| startime   | When the conversation was started                                       | timestamp            |
| endtime    | When the conversation was finished                                      | timestamp            |


***messages***

| Field   | Description                                                            | Type                 |
|---------|------------------------------------------------------------------------|----------------------|
| id      | Primary Key                                                            | autoincrementing int |
| log_id  | logs.id foreign key                                                    | int                  |
| type    | The bitwise mask of message type - eg 0x01 for sent, 0x02 for received | int                  |
| who     | Them who spoke                                                         | varchar              |
| message | The (HTML) content of the message                                      | text                 |
| time    | The timestamp of the message                                           | timestamp            |

(see [PurpleMessageFlags](https://developer.pidgin.im/doxygen/2.7.11/html/conversation_8h-source.html#l00105) for more info about the `type` bitmask)
