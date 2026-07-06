#pragma once

// Resource identifiers for the Win32 front end.
// 1 is reserved for the application manifest (CREATEPROCESS_MANIFEST_RESOURCE_ID).

// Main window child controls.
#define IDC_TIMELINES_LIST 1001
#define IDC_TIMELINE_VIEW  1002

// Add Account dialog.
#define IDD_ADD_ACCOUNT    200
#define IDC_PLATFORM_COMBO 201
#define IDC_SERVICE_EDIT   202
#define IDC_HANDLE_EDIT    203
#define IDC_APPPASS_EDIT   204
#define IDC_SERVICE_LABEL  205
#define IDC_HANDLE_LABEL   206
#define IDC_APPPASS_LABEL  207

// Compose dialog.
#define IDD_COMPOSE            220
#define IDC_COMPOSE_CONTEXT    221
#define IDC_COMPOSE_EDIT       222
#define IDC_COMPOSE_CW_LABEL   223
#define IDC_COMPOSE_CW         224
#define IDC_COMPOSE_POST_LABEL 225
#define IDC_COMPOSE_VIS_LABEL  226
#define IDC_COMPOSE_VISIBILITY 227
#define IDC_COMPOSE_LANG_LABEL 228
#define IDC_COMPOSE_LANGUAGE   229
#define IDC_COMPOSE_POLL       230
#define IDC_COMPOSE_POLL_OPT1  231
#define IDC_COMPOSE_POLL_OPT2  232
#define IDC_COMPOSE_POLL_OPT3  233
#define IDC_COMPOSE_POLL_OPT4  234
#define IDC_COMPOSE_POLL_MULTI 235
#define IDC_COMPOSE_DUR_LABEL  236
#define IDC_COMPOSE_DURATION   237
#define IDC_COMPOSE_SCHEDULE   238
#define IDC_COMPOSE_SCHED_TIME 239
#define IDC_COMPOSE_COUNTER    250
#define IDC_COMPOSE_RCP_LABEL  251
#define IDC_COMPOSE_RECIPIENTS 252
#define IDC_COMPOSE_MEDIA      253
#define IDC_COMPOSE_MEDIA_STATUS 254

// Attachments dialog (add media + alt text) launched from the compose dialog.
#define IDD_ATTACHMENTS        530
#define IDC_ATT_LIST           531
#define IDC_ATT_ADD            532
#define IDC_ATT_REMOVE         533
#define IDC_ATT_ALT_LABEL      534
#define IDC_ATT_ALT            535
#define IDC_ATT_ALT_SET        536

// New Timeline dialog.
#define IDD_NEW_TIMELINE   240
#define IDC_TIMELINE_TYPE  241
#define IDC_TIMELINE_VALUE 242
#define IDC_TIMELINE_VALUE_LABEL 243

// Settings property sheet pages.
#define IDD_SET_GENERAL     300
#define IDC_SET_CMDRETURN   301
#define IDD_SET_TIMELINES   310
#define IDC_SET_CACHELIMIT  311
#define IDC_SET_CACHELIMIT_SPIN 312
#define IDC_SET_AUTOREFRESH 313
#define IDC_SET_STREAMING   314
#define IDC_SET_SHOW_MENTIONS 315
#define IDC_SET_REVERSE       316
#define IDC_SET_AUTOLOAD      317
#define IDD_SET_AUDIO       320
#define IDC_SET_SOUNDS      321
#define IDC_SET_SOUNDPACK   322
#define IDC_SET_OPENPACKS   323
#define IDC_SET_VOLUME_LABEL 324
#define IDC_SET_VOLUME      325
#define IDC_SET_BOUNDARY    326
#define IDD_SET_SPEECH      330
#define IDC_SET_SPEECH_LIST 331
#define IDC_SET_SPEECH_UP   332
#define IDC_SET_SPEECH_DOWN 333
#define IDC_SET_SPEECH_POSTS  334
#define IDC_SET_SPEECH_USERS  335
#define IDC_SET_SPEECH_NOTIFS 336
#define IDC_SET_CW_MODE       337
#define IDC_SET_POST_EMOJI    338
#define IDC_SET_NAME_EMOJI    339
#define IDC_SET_MAX_MENTIONS  344
#define IDC_SET_MAX_MENTIONS_SPIN 345
#define IDC_SET_REPEAT_EDGE   346
#define IDC_SET_ABSOLUTE_TIME 347
#define IDC_SET_SEPARATOR     348

#define IDD_SPEECH_DETAIL     380
#define IDC_SPEECH_DETAIL_LIST 381
#define IDC_SPEECH_DETAIL_UP  382
#define IDC_SPEECH_DETAIL_DOWN 383
#define IDC_SPEECH_DETAIL_BEFORE 384
#define IDC_SPEECH_DETAIL_AFTER  385
#define IDC_SPEECH_DETAIL_NOSEP  386
#define IDD_SET_ADVANCED    340
#define IDC_SET_FETCHPAGES  341
#define IDC_SET_FETCHPAGES_SPIN 342
#define IDD_SET_CONFIRM     350
#define IDC_SET_CONFIRM_BOOST 351
#define IDC_SET_CONFIRM_FAV   352
#define IDC_SET_CONFIRM_CLEAR 353
#define IDC_SET_CONFIRM_BLOCK 354
#define IDC_SET_CONFIRM_UNBOOST 700
#define IDC_SET_CONFIRM_UNFAV   701
#define IDC_SET_CONFIRM_UNBLOCK 702
#define IDC_SET_CONFIRM_DELETE  703
#define IDD_SET_INVISIBLE   355
#define IDC_SET_INVIS_MODE  356
#define IDC_SET_INVIS_LAYERKEY 357
#define IDC_SET_INVIS_MANAGER 358

#define IDD_SET_UPDATES     390
#define IDC_SET_UPDATE_BRANCH 391
#define IDC_SET_UPDATE_STARTUP 392

#define IDD_SET_BEHAVIOR    393
#define IDC_SET_ENTER_POST  394
#define IDC_SET_ENTER_USER  395
#define IDC_SET_SECOND_POST 396
#define IDC_SET_MEDIA_BG    397
#define IDC_SET_REPLY_MENTIONS_END 398

// Keyboard Manager dialog + its binding-capture sub-dialog.
#define IDD_KEYMAP_MANAGER  400
#define IDC_KM_KEYMAP       401
#define IDC_KM_NEW          402
#define IDC_KM_DELETE       403
#define IDC_KM_STATUS       404
#define IDC_KM_LIST         405
#define IDC_KM_SET          406
#define IDC_KM_UNBIND       407
#define IDC_KM_RESET        408
#define IDC_KM_SAVE         409
#define IDC_KM_IMPORT       410
#define IDD_KM_BINDING      420
#define IDC_KMB_CTRL        421
#define IDC_KMB_ALT         422
#define IDC_KMB_SHIFT       423
#define IDC_KMB_WIN         424
#define IDC_KMB_KEY         425
#define IDC_KMB_CURRENT     426
#define IDD_KM_NEWNAME      430
#define IDC_KMN_NAME        431

// Find-in-timeline dialog.
#define IDD_FIND            440
#define IDC_FIND_TEXT       441

// Layer keys help window.
#define IDD_LAYER_HELP      495
#define IDC_LAYERHELP_TEXT  496

// Client Filters dialog (per-timeline display filter).
#define IDD_CLIENT_FILTER   450
#define IDC_CF_ORIGINAL     451
#define IDC_CF_REPLIES      452
#define IDC_CF_REPLIES_ME   453
#define IDC_CF_THREADS      454
#define IDC_CF_BOOSTS       455
#define IDC_CF_QUOTES       456
#define IDC_CF_MEDIA        457
#define IDC_CF_NO_MEDIA     458
#define IDC_CF_MY_POSTS     459
#define IDC_CF_MY_REPLIES   460
#define IDC_CF_TEXT         461
#define IDC_CF_CLEAR        462

// Server Filters manager (Mastodon /api/v2/filters).
#define IDD_SERVER_FILTERS  470
#define IDC_SF_LIST         471
#define IDC_SF_ADD          472
#define IDC_SF_EDIT         473
#define IDC_SF_DELETE       474
#define IDC_SF_STATUS       475

// Add/Edit server filter sub-dialog.
#define IDD_EDIT_FILTER     480
#define IDC_EF_TITLE        481
#define IDC_EF_KEYWORDS     482
#define IDC_EF_WHOLE_WORD   483
#define IDC_EF_CTX_HOME     484
#define IDC_EF_CTX_NOTIF    485
#define IDC_EF_CTX_PUBLIC   486
#define IDC_EF_CTX_THREAD   487
#define IDC_EF_CTX_ACCOUNT  488
#define IDC_EF_ACTION       489
#define IDC_EF_EXPIRES      490

// Post Info dialog.
#define IDD_POST_INFO         360
#define IDC_POSTINFO_TEXT     361
#define IDC_POSTINFO_REPLY    362
#define IDC_POSTINFO_BOOST    363
#define IDC_POSTINFO_FAVORITE 364
#define IDC_POSTINFO_QUOTE    365
#define IDC_POSTINFO_BROWSER  366
#define IDC_POSTINFO_THREAD   367
#define IDC_POSTINFO_AUTHOR   368
#define IDC_POSTINFO_LINKS    369
#define IDC_POSTINFO_DELETE   705

#define IDD_USER_PROFILE      370
#define IDC_PROFILE_TEXT      371
#define IDC_PROFILE_POSTS     372
#define IDC_PROFILE_BROWSER   373
#define IDC_PROFILE_FOLLOW    374
#define IDC_PROFILE_MUTE      375
#define IDC_PROFILE_BLOCK     376
#define IDC_PROFILE_BOOSTS    377
#define IDC_PROFILE_FOLLOWERS 378
#define IDC_PROFILE_FOLLOWING 379
#define IDC_PROFILE_LISTS     500

// User list-membership dialog (add/remove a user from your Mastodon lists).
#define IDD_LIST_MEMBERSHIP   501
#define IDC_LM_LIST           502
#define IDC_LM_HEADING        503

// List Manager (Application menu) + its create/edit sub-dialog.
#define IDD_LISTS_MANAGER     510
#define IDC_LSM_LIST          511
#define IDC_LSM_OPEN          512
#define IDC_LSM_NEW           513
#define IDC_LSM_EDIT          514
#define IDC_LSM_DELETE        515
#define IDD_LIST_EDIT         516
#define IDC_LSE_NAME          517
#define IDC_LSE_REPLIES       518
#define IDC_LSE_EXCLUSIVE     519

// Follow-a-hashtag prompt (editable combo pre-filled with the post's hashtags).
#define IDD_FOLLOW_HASHTAG    540
#define IDC_FH_LABEL          541
#define IDC_FH_COMBO          542

// Followed Hashtags manager (Application menu): list + open-timeline / unfollow.
#define IDD_FOLLOWED_HASHTAGS 545
#define IDC_FHM_LIST          546
#define IDC_FHM_OPEN          547
#define IDC_FHM_UNFOLLOW      548

// "Type a handle" prompt (manual user entry for timeline / profile / follow).
#define IDD_ENTER_HANDLE      550
#define IDC_EH_LABEL          551
#define IDC_EH_EDIT           552

// Per-account settings dialog (soundpack for the focused account).
#define IDD_ACCOUNT_SETTINGS  555
#define IDC_AS_SOUND_LABEL    556
#define IDC_AS_SOUNDPACK      557
#define IDC_AS_OPENPACKS      558
