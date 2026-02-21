import os

def configure(env):
    project_dir = env.get("PROJECT_DIR") or env.get("PIOENV")
    if not project_dir:
        project_dir = env['PROJECT_DIR'] if 'PROJECT_DIR' in env else None
    tools_dir = os.path.join(env['PROJECT_DIR'], "tools")
    env.PrependENVPath("PATH", tools_dir)

def before_build(env, **kwargs):
    # Ensure the tools dir is present in PATH during build and post-build steps
    tools_dir = os.path.join(env['PROJECT_DIR'], "tools")
    env.PrependENVPath("PATH", tools_dir)
