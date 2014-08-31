﻿using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Data;
using System.Text;
using System.Web;
using System.Web.UI;
using System.Web.UI.WebControls;
using System.Data.SqlClient;
using System.Configuration;

public partial class api_Login : WOApiWebPage
{
    protected override void Execute()
    {
        string username = web.Param("username");
        string password = web.Param("password");
        string serverkey = web.Param("serverkey");
        string computerid = web.Param("computerid");
        string mac = web.Param("mac");  

        SqlCommand sqcmd = new SqlCommand();
        sqcmd.CommandType = CommandType.StoredProcedure;
        sqcmd.CommandText = "WZ_ACCOUNT_LOGIN";
        sqcmd.Parameters.AddWithValue("@in_IP", LastIP);
        sqcmd.Parameters.AddWithValue("@in_EMail", username);
        sqcmd.Parameters.AddWithValue("@in_Password", password);
        sqcmd.Parameters.AddWithValue("@in_HardwareID", computerid);
        sqcmd.Parameters.AddWithValue("@in_Mac", mac);  
        if (!CallWOApi(sqcmd))
            return;

        reader.Read();
        int CustomerID = getInt("CustomerID"); ;
        int AccountStatus = getInt("AccountStatus");
        int SessionID = 0;

        if (CustomerID > 0)
        {
            SessionID = getInt("SessionID");
        }

        GResponse.Write("WO_0");
        GResponse.Write(string.Format("{0} {1} {2}",
            CustomerID, SessionID, AccountStatus));
    }
}
