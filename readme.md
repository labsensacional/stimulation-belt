# Collar de Estimulación

En este proyecto mostramos como clonar el control de un collar de adiestramiento que tiene opciones de estimulos por vibracion, sonido e impulsos electricos.

## Captura de la señal

Para esto usamos el [Trainer 3](https://mundomascotas.com.ar/tienda/collar-de-adiestramiento-para-perros/trainer-3/) y el [flipper zero](https://en.wikipedia.org/wiki/Flipper_Zero) que amablemente nos prestaron Matt y Gia respectivamente.

[KinkyToyMaker](https://www.instagram.com/kinkytoymaker/?hl=en) grabó las señales que estan en la carpeta `subs`.

![](demo.jpg)

Compramos el collar sin control [acá](https://mundomascotas.com.ar/tienda/collares-extras/collar-extra-trainer3/)

## Hardware

Compramos distintos modulos RF de 433 MHZ para OOK y lo conectamos a un Arduino Uno. Para debuggear y lograr que termine de funcionar todo nos guiamos de [este tutorial](https://youtu.be/LbCDpbWrdlQ?si=hX0qrkrM-xSeYEQN), el cual plantea conectar a la placa de sonido el receptor para grabar las señales que se reciben y comparar las del control que se quiere clonar con la de nuestro emisor.

- https://www.mercadolibre.com.ar/modulo-rf-transmisor-y-receptor-433-mhz-arduino/p/MLA32487770#polycard_client=search-nordic&searchVariation=MLA32487770&wid=MLA1665965578&position=11&search_layout=stack&type=product&tracking_id=eee82f79-bf93-46d6-a4bf-ca5ad650e982&sid=search
    - De este anduvo el receptor y no el transmisor

- https://articulo.mercadolibre.com.ar/MLA-780605460-emisor-receptor-superheterodino-433-armodltxrxwl102-_JM?quantity=1
    - De este anduvo el transmisor y no el receptor

