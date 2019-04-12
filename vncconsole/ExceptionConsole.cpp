/////////////////////////////////////////////////////////////////////////////
//  Copyright (C) 2002-2013 UltraVNC Team Members. All Rights Reserved.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.
//
// If the source code for the program is not available from the place from
// which you received this file, check 
// http://www.uvnc.com/
//
////////////////////////////////////////////////////////////////////////////


#include "stdafx.h"
#include "ExceptionConsole.h"
#include "messbox.h"

ExceptionConsole::ExceptionConsole(const char *info,int error_nr) : m_error_nr(-1)
{
	assert(info != NULL);
	m_info = new char[strlen(info)+1];
	strcpy(m_info, info);
    if (error_nr)
	m_error_nr=error_nr;
}

ExceptionConsole::~ExceptionConsole()
{
	delete [] m_info;
}

// ---------------------------------------


QuietExceptionConsole::QuietExceptionConsole(const char *info,int error_nr) : ExceptionConsole(info,error_nr)
{

}

QuietExceptionConsole::~QuietExceptionConsole()
{

}

void QuietExceptionConsole::Report()
{
#ifdef _MSC_VER
	_RPT1(_CRT_WARN, "Warning : %s\n", m_info);
#endif
}

// ---------------------------------------

WarningExceptionConsole::WarningExceptionConsole(const char *info,int error_nr, bool close) : ExceptionConsole(info,error_nr)
{
	m_close = close;
}

WarningExceptionConsole::~WarningExceptionConsole()
{

}

void WarningExceptionConsole::Report()
{
#ifdef _MSC_VER
	_RPT1(_CRT_WARN, "Warning : %s\n", m_info);
#endif
	// ShowMessageBox2(m_info,m_error_nr);
    // Todo >> Printout?
	//MessageBox(NULL, m_info, " UltraVNC Info", MB_OK| MB_ICONEXCLAMATION | MB_SETFOREGROUND | MB_TOPMOST);
}

// ---------------------------------------

ErrorExceptionConsole::ErrorExceptionConsole(const char *info,int error_nr) : ExceptionConsole(info,error_nr)
{

}

ErrorExceptionConsole::~ErrorExceptionConsole()
{

}

void ErrorExceptionConsole::Report()
{
#ifdef _MSC_VER
	_RPT1(_CRT_WARN, "Warning : %s\n", m_info);
#endif
	//ShowMessageBox2(m_info,m_error_nr);
    // Todo
	//MessageBox(NULL, m_info, " UltraVNC Info", MB_OK | MB_ICONSTOP | MB_SETFOREGROUND | MB_TOPMOST);
}
