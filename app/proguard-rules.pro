# Add project specific ProGuard rules here.
-keep class com.nexusvm.engine.NexusEngine { *; }
-keepclassmembers class com.nexusvm.engine.NexusEngine {
    native <methods>;
}
