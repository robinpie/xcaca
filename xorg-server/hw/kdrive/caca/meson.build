srcs = [
    'caca.c',
    'cacainit.c',
    'cacainput.c',
    'caca_host.c',
]

xcaca_dep = [
    common_dep,
    dependency('caca'),
]

executable(
    'Xcaca',
    srcs,
    include_directories: [inc, include_directories('../src')],
    dependencies: xcaca_dep,
    link_with: [
        libxserver_main,
        libxserver_exa,
        kdrive,
        libxserver_fb,
        libxserver,
        libxserver_config,
        libxserver_xkb_stubs,
        libxserver_xi_stubs,
        libxserver_glx,
        libglxvnd,
    ],
    install: true,
)
