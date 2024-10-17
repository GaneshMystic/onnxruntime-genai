﻿using Microsoft.ML.OnnxRuntimeGenAI;
using System;
using System.Runtime.InteropServices;

/// <summary>
/// A container of adapters.
/// </summary>
public class Adapters : SafeHandle
{
    private Adapters(IntPtr handle) :
        base(handle, true)
    {
    }

    /// <summary>
    /// Creates a container for adapters
    /// used to load, unload and hold them.
    /// Throws on error.
    /// </summary>
    /// <param name="model">Reference to a loaded model</param>
    /// <returns>new Adapters object</returns>
    public static Adapters Create(Model model)
    {
        Result.VerifySuccess(NativeMethods.OgaCreateAdapters(model.Handle, out IntPtr handle));
        return new Adapters(handle);
    }

    /// <summary>
    /// Method that loads adapter data and assigns it a nmae that
    /// it can be referred to. Throws on error.
    /// </summary>
    /// <param name="adapterPath">file path to load</param>
    /// <param name="adapterName">adapter name</param>
    public void LoadAdapter(string adapterPath, string adapterName)
    {
        Result.VerifySuccess(NativeMethods.OgaLoadAdapter(handle, 
            StringUtils.ToUtf8(adapterPath), StringUtils.ToUtf8(adapterName)));
    }

    /// <summary>
    /// Unload the adatper that was loaded by the LoadAdapter method.
    /// Throws on error.
    /// </summary>
    /// <param name="adapterName"></param>
    public void UnloadAdapter(string adapterName)
    {
        Result.VerifySuccess(NativeMethods.OgaUnloadAdapter(handle, StringUtils.ToUtf8(adapterName)));
    }

    internal IntPtr Handle { get { return handle; } }

    /// <summary>
    /// Implement SafeHandle override
    /// </summary>
    public override bool IsInvalid => handle == IntPtr.Zero;

    protected override bool ReleaseHandle()
    {
        NativeMethods.OgaDestroyAdapters(handle);
        handle = IntPtr.Zero;
        return true;
    }
}
