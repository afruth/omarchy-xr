"""Built-in, immutable-by-convention workspace templates; return fresh values."""


def built_in_setups():
    def row(sizes, curve=0):
        monitors=[]
        x=0
        height=max(h for w,h in sizes)
        for index,(w,h) in enumerate(sizes):
            monitors.append({'id':str(index+1),'width':w,'height':h,'x':x,'y':(height-h)//2,
                             'scale':1,'brightness':100,'curvature':curve})
            x+=w+30
        return {'version':1,'fps':60,'spacing':30,'curvature':0,'monitors':monitors}
    fhd=(1920,1080)
    choices=[
        ('one-fhd','Full HD','1920 × 1080',[fhd],0),
        ('one-4k','4K','3840 × 2160',[(3840,2160)],0),
        ('three-fhd','Three Full HD','3 × 1920 × 1080',[fhd]*3,0),
        ('two-fhd','Two Full HD','2 × 1920 × 1080',[fhd]*2,0),
        ('portrait-sides','Full HD + portrait sides','1080 × 1920 · 1920 × 1080 · 1080 × 1920',[(1080,1920),fhd,(1080,1920)],0),
        ('wide-curved','Wide curved','3440 × 1440 · 21:9',[(3440,1440)],75),
        ('superwide-curved','Superwide curved','5120 × 1440 · 32:9',[(5120,1440)],85),
        ('4k-side','4K + portrait side','3840 × 2160 + 1440 × 2160',[(3840,2160),(1440,2160)],0),
    ]
    return [{'id':'builtin:'+key,'name':name,'description':description,'layout':row(sizes,curve)}
            for key,name,description,sizes,curve in choices]
