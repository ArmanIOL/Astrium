========
astrium
========

-----------------------------------------
A simple, clean, beautiful view of the web
-----------------------------------------

:Date: November 2026
:Copyright: Copyright 2026 ArmanIOL
:Copyright: Copyright 2006-2025 GNOME Foundation
:Manual section: 1

SYNOPSIS
--------

|  **astrium** [*OPTION*...] [*URL*...]

DESCRIPTION
-----------

**astrium** is a web browser for Linux, built on WebKitGTK and GTK 4. It is a
fork of GNOME Web (Epiphany) and shares its rendering engine and much of its
code.

OPTIONS
-------

``--new-window``

  Open a new browser window instead of a new tab

``-l``, ``--load-session``\ =\ *FILE*

  Load the given session state file

``-i``, ``--incognito-mode``

  Start an instance with read-only user data, for browsing without storing history on your computer

``-p``, ``--private-instance``

  Start a temporary instance without any user data, for testing

``-a``, ``--application-mode``\ =\ *BASENAME*

  Start a private instance in web application mode (requires passing either desktop file basename or ``--profile``)

``--automation-mode``

  Start a private instance for WebDriver control

``--profile``\ =\ *FILE*

  Start using a custom profile directory for user data

``--search``\ =\ *TERM*

  Initiate search with given search term

``-?``, ``--help``

  Show help options

``--version``

  Show version

Astrium has a help system. Run the browser and select ``Help`` from the menu.

AUTHOR
------

Astrium is maintained by ArmanIOL. The browser is derived from Epiphany, which
was developed by Marco Pesenti Gritti (in memoriam), Christian Persch,
Xan Lopez, Carlos Garcia Campos, Claudio Saavedra, Michael Catanzaro,
Jan-Michael Brummer, Alice Mikhaylenko, Patrick Griffis, and many others.