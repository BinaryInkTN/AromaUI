package {{PACKAGE_NAME}};

import android.app.Activity;
import android.app.NativeActivity;
import android.os.Build;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.content.Intent;
import android.graphics.Bitmap;
import android.net.Uri;
import android.nfc.NfcAdapter;
import android.nfc.Tag;
import android.os.Bundle;

public class AromaActivity extends NativeActivity {
    public static final int REQ_PERMISSION_CB = 1001;
    public static final int REQ_PICK_IMAGE = 2001;
    public static final int REQ_CAPTURE_PHOTO = 2002;
    public static final int REQ_OPEN_DOCUMENT = 2003;
    public static final int REQ_PICK_CONTACT = 2005;
    private static native void nativeOnPermissionResult(int requestCode, String[] permissions, int[] grantResults);
    private static native void nativeOnTextInput(String text);
    private static native void nativeOnBackspace();
    private static native void nativeOnEditorDone();
    public static void imeText(String text) {
        try {
            nativeOnTextInput(text);
        } catch (UnsatisfiedLinkError e) {
        }
    }
    public static void imeBackspace() {
        try {
            nativeOnBackspace();
        } catch (UnsatisfiedLinkError e) {
        }
    }
    public static void imeDone() {
        try {
            nativeOnEditorDone();
        } catch (UnsatisfiedLinkError e) {
        }
    }
    private static native void nativeOnActivityResult(int requestCode, int resultCode, String data, String extra);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        handleNfcIntent(getIntent());
    }

    @Override
    protected void onResume() {
        super.onResume();
        AromaHelper.nfcStart(this);
        handleNfcIntent(getIntent());
    }

    @Override
    protected void onPause() {
        AromaHelper.nfcStop(this);
        super.onPause();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleNfcIntent(intent);
    }

    private void handleNfcIntent(Intent intent) {
        if (intent == null) return;
        try {
            String action = intent.getAction();
            if (NfcAdapter.ACTION_NDEF_DISCOVERED.equals(action)
                || NfcAdapter.ACTION_TECH_DISCOVERED.equals(action)
                || NfcAdapter.ACTION_TAG_DISCOVERED.equals(action)) {
                Tag tag = intent.getParcelableExtra(NfcAdapter.EXTRA_TAG);
                String payload = AromaHelper.nfcReadText(tag);
                AromaHelper.dispatchNfc(payload);
            }
        } catch (Exception e) {
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        try {
            nativeOnPermissionResult(requestCode, permissions, grantResults);
        } catch (UnsatisfiedLinkError e) {
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        String out = null;
        String extra = null;
        if (resultCode == Activity.RESULT_OK && data != null) {
            if (requestCode == REQ_PICK_IMAGE || requestCode == REQ_OPEN_DOCUMENT) {
                Uri uri = data.getData();
                if (uri != null) {
                    if (requestCode == REQ_OPEN_DOCUMENT) {
                        try {
                            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                        } catch (Exception e) {
                        }
                    }
                    out = AromaHelper.copyUriToCache(this, uri, requestCode == REQ_PICK_IMAGE ? "aroma_pick_" : "aroma_doc_");
                }
            } else if (requestCode == REQ_CAPTURE_PHOTO) {
                Bundle ex = data.getExtras();
                if (ex != null) {
                    Object bmp = ex.get("data");
                    if (bmp instanceof Bitmap) {
                        out = AromaHelper.saveBitmapToCache(this, (Bitmap) bmp, "aroma_capture_");
                    }
                }
            } else if (requestCode == REQ_PICK_CONTACT) {
                Uri uri = data.getData();
                if (uri != null) {
                    extra = AromaHelper.contactsResolve(this, uri);
                }
            }
        }
        try {
            nativeOnActivityResult(requestCode, resultCode, out, null);
        } catch (UnsatisfiedLinkError e) {
        }
    }
}
